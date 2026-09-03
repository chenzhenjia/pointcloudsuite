#include <pcv/camera/mv3d_lp_camera.h>

#include <pcv/camera/frame_converter.h>
#include <pcv/camera/frame_data.h>

#include "live_frame_policy.h"
#include "scan_frame_assembler.h"
#include "trigger_mode_policy.h"

#include <QDateTime>
#include <QDebug>
#include <QString>

#include <Mv3dLpApi.h>
#include <Mv3dLpDefine.h>
#include <Mv3dLpImgProc.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace pcv::camera {

namespace {

// The SDK initialization/finalization pair is process-global. Keep a module
// reference count so multiple camera instances share one Initialize().
std::mutex g_sdkMutex;
int g_sdkRefCount = 0;
bool g_sdkInitialized = false;

bool ensureSdkInitialized(CameraError *error)
{
    std::lock_guard<std::mutex> lock(g_sdkMutex);
    if (g_sdkInitialized) {
        ++g_sdkRefCount;
        return true;
    }
    if (MV3D_LP_Initialize() != MV3D_LP_OK) {
        if (error) error->message = QStringLiteral("MV3D_LP_Initialize failed");
        return false;
    }
    g_sdkInitialized = true;
    ++g_sdkRefCount;
    return true;
}

void releaseSdk()
{
    std::lock_guard<std::mutex> lock(g_sdkMutex);
    if (g_sdkRefCount > 0) --g_sdkRefCount;
    if (g_sdkRefCount == 0 && g_sdkInitialized) {
        MV3D_LP_Finalize();
        g_sdkInitialized = false;
    }
}

QString statusText(MV3D_LP_STATUS status)
{
    const auto code = quint32(status);
    QString name;
    switch (code) {
    case quint32(MV3D_LP_OK): name = QStringLiteral("MV3D_LP_OK"); break;
    case quint32(MV3D_LP_E_HANDLE): name = QStringLiteral("MV3D_LP_E_HANDLE"); break;
    case quint32(MV3D_LP_E_SUPPORT): name = QStringLiteral("MV3D_LP_E_SUPPORT"); break;
    case quint32(MV3D_LP_E_PARAMETER): name = QStringLiteral("MV3D_LP_E_PARAMETER"); break;
    case quint32(MV3D_LP_E_ABNORMAL_IMAGE): name = QStringLiteral("MV3D_LP_E_ABNORMAL_IMAGE"); break;
    case quint32(MV3D_LP_E_ALGORITHM): name = QStringLiteral("MV3D_LP_E_ALGORITHM"); break;
    case quint32(MV3D_LP_E_DEVICE_OFFLINE): name = QStringLiteral("MV3D_LP_E_DEVICE_OFFLINE"); break;
    default: name = QStringLiteral("MV3D_LP_STATUS_UNKNOWN"); break;
    }
    return QStringLiteral("0x%1 (%2)").arg(code, 8, 16, QLatin1Char('0')).arg(name);
}

bool hasExactRasterPayload(const MV3D_LP_IMAGE_DATA &image,
                           std::uint32_t bytesPerSample)
{
    if (!image.pData || image.nWidth == 0 || image.nHeight == 0) return false;
    const std::uint64_t expected = std::uint64_t(image.nWidth)
        * std::uint64_t(image.nHeight)
        * bytesPerSample;
    return expected <= std::numeric_limits<std::uint32_t>::max()
        && image.nDataLen == expected;
}

// Owned frame copy performed on the SDK callback thread. The optional source
// owns transport metadata when the SDK conversion output does not populate it.
std::shared_ptr<CameraFrame> copyFrame(const MV3D_LP_IMAGE_DATA &image,
                                       const MV3D_LP_IMAGE_DATA *source = nullptr)
{
    const MV3D_LP_IMAGE_DATA &metadata = source ? *source : image;
    auto frame = std::make_shared<CameraFrame>();
    frame->frameType = FrameType(image.enImageType);
    frame->width = image.nWidth;
    frame->height = image.nHeight;
    frame->frameNumber = metadata.nFrameNum;
    frame->timeStamp = qint64(metadata.nTimeStamp);
    frame->valid = metadata.bValid != 0;
    frame->xScale = metadata.fXScale;
    frame->yScale = metadata.fYScale;
    frame->zScale = metadata.fZScale;
    frame->xOffset = metadata.nXOffset;
    frame->yOffset = metadata.nYOffset;
    frame->zOffset = metadata.nZOffset;
    if (image.pData && image.nDataLen > 0) {
        frame->data = QByteArray(reinterpret_cast<const char *>(image.pData),
                                 qsizetype(image.nDataLen));
    }
    if (metadata.pIntensityData && metadata.nIntensityDataLen > 0) {
        frame->intensityData = QByteArray(
            reinterpret_cast<const char *>(metadata.pIntensityData),
            qsizetype(metadata.nIntensityDataLen));
    }
    return frame;
}

std::shared_ptr<CameraFrame> makeInvalidFrame(const MV3D_LP_IMAGE_DATA &image,
                                              const QString &error)
{
    auto frame = copyFrame(image);
    frame->data.clear();
    frame->intensityData.clear();
    frame->valid = false;
    // FrameKind remains Frame so the UI reports a conversion error instead of
    // treating a data issue as a device-disconnect event.
    frame->exceptionDescription = error;
    return frame;
}

struct CallbackContext {
    std::atomic<bool> accepting{false};
    std::atomic<int> inFlight{0};
    std::mutex mutex;
    std::condition_variable condition;
    LatestFrameSlot *slot = nullptr;
    std::mutex conversionMutex;
    std::mutex assemblyMutex;
    detail::ScanFrameAssembler assembler;
    std::optional<ImageMode> activeImageMode;
    std::mutex diagnosticsMutex;
    QString lastConversionKey;
    std::uint32_t matchingConversionCount = 0;
    bool hasProfileScanBounds = false;
    float profileMinimum[3]{};
    float profileMaximum[3]{};

    void logConversion(const MV3D_LP_IMAGE_DATA &input,
                       const MV3D_LP_IMAGE_DATA *output,
                       MV3D_LP_STATUS status)
    {
        const auto outputType = output
            ? std::uint32_t(output->enImageType)
            : std::uint32_t(FrameType::Undefined);
        const QString key = QStringLiteral("raw=0x%1 out=0x%2 valid=%3 status=%4")
            .arg(std::uint32_t(input.enImageType), 8, 16, QLatin1Char('0'))
            .arg(outputType, 8, 16, QLatin1Char('0'))
            .arg(input.bValid != 0)
            .arg(statusText(status));
        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        if (key == lastConversionKey) {
            ++matchingConversionCount;
        } else {
            lastConversionKey = key;
            matchingConversionCount = 1;
        }
        if (matchingConversionCount == 1 || matchingConversionCount % 120 == 0) {
            qInfo().noquote() << "Mv3dLpCamera: callback conversion"
                              << key
                              << "rawSize=" << input.nWidth << "x" << input.nHeight
                              << "rawLen=" << input.nDataLen
                              << "outSize=" << (output ? output->nWidth : 0) << "x"
                              << (output ? output->nHeight : 0)
                              << "outLen=" << (output ? output->nDataLen : 0)
                              << "count=" << matchingConversionCount;
        }
    }

    void configureAssembly(std::uint32_t expectedRows)
    {
        std::lock_guard<std::mutex> lock(assemblyMutex);
        assembler.configure(expectedRows);
    }

    void configureActiveImageMode(std::optional<ImageMode> mode)
    {
        std::lock_guard<std::mutex> lock(assemblyMutex);
        activeImageMode = mode;
    }

    void resetAssembly()
    {
        std::lock_guard<std::mutex> lock(assemblyMutex);
        assembler.reset();
        hasProfileScanBounds = false;
    }

    bool acceptsRawType(FrameType type) const
    {
        if (activeImageMode == ImageMode::RangeImage) {
            return type == FrameType::DepthC16;
        }
        if (activeImageMode == ImageMode::PointCloudImage) {
            return type == FrameType::PointCloudABC32f || type == FrameType::ProfileABC16;
        }
        return false;
    }

    void logProfileBounds(const MV3D_LP_IMAGE_DATA &output,
                          std::uint32_t sourceFrameNumber)
    {
        if (!output.pData || output.nDataLen % 12 != 0) return;
        const auto *coordinates = reinterpret_cast<const float *>(output.pData);
        float minimum[3]{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
        float maximum[3]{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest()};
        bool hasPoint = false;
        for (std::uint32_t index = 0; index < output.nDataLen / 12; ++index) {
            const float x = coordinates[index * 3];
            const float y = coordinates[index * 3 + 1];
            const float z = coordinates[index * 3 + 2];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
            minimum[0] = qMin(minimum[0], x); minimum[1] = qMin(minimum[1], y); minimum[2] = qMin(minimum[2], z);
            maximum[0] = qMax(maximum[0], x); maximum[1] = qMax(maximum[1], y); maximum[2] = qMax(maximum[2], z);
            hasPoint = true;
        }
        if (!hasPoint) return;

        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        if (sourceFrameNumber == 0 || !hasProfileScanBounds) {
            std::copy(std::begin(minimum), std::end(minimum), std::begin(profileMinimum));
            std::copy(std::begin(maximum), std::end(maximum), std::begin(profileMaximum));
            hasProfileScanBounds = true;
        } else {
            for (int axis = 0; axis < 3; ++axis) {
                profileMinimum[axis] = qMin(profileMinimum[axis], minimum[axis]);
                profileMaximum[axis] = qMax(profileMaximum[axis], maximum[axis]);
            }
        }
        if (sourceFrameNumber == 0 || sourceFrameNumber % 120 == 0) {
            qInfo() << "Mv3dLpCamera: Profile diagnostic bounds"
                    << "batchRows=" << output.nHeight << "startRow=" << sourceFrameNumber
                    << "batchXyz=[" << minimum[0] << minimum[1] << minimum[2] << "]-["
                    << maximum[0] << maximum[1] << maximum[2] << "]"
                    << "scanXyz=[" << profileMinimum[0] << profileMinimum[1] << profileMinimum[2] << "]-["
                    << profileMaximum[0] << profileMaximum[1] << profileMaximum[2] << "]";
        }
    }

    void publishNormalizedFrame(std::shared_ptr<CameraFrame> frame)
    {
        if (!frame || !slot) return;
        detail::ScanAssemblyResult assembly;
        {
            std::lock_guard<std::mutex> lock(assemblyMutex);
            assembly = assembler.append(frame);
        }
        if (assembly.outcome == detail::ScanAssemblyOutcome::Complete) {
            qInfo() << "Mv3dLpCamera: complete scan"
                    << "rows=" << assembly.frame->accumulatedScanRows << "/"
                    << assembly.frame->expectedScanRows
                    << "fragments=" << assembly.frame->scanFragmentCount
                    << "discarded=" << assembly.frame->discardedScanCount
                    << "size=" << assembly.frame->width << "x" << assembly.frame->height;
            slot->publish(std::move(assembly.frame));
            return;
        }
        if (assembly.outcome == detail::ScanAssemblyOutcome::Rejected) {
            auto rejected = std::make_shared<CameraFrame>(*frame);
            rejected->data.clear();
            rejected->intensityData.clear();
            rejected->valid = false;
            rejected->completeScan = false;
            rejected->exceptionDescription = assembly.error;
            qWarning().noquote() << "Mv3dLpCamera: discarded incomplete scan:" << assembly.error;
            slot->publish(std::move(rejected));
        }
    }
};

class CallbackGuard {
public:
    explicit CallbackGuard(CallbackContext *context) : m_context(context)
    {
        if (!m_context) return;
        std::lock_guard<std::mutex> lock(m_context->mutex);
        if (!m_context->accepting.load(std::memory_order_acquire)) return;
        m_context->inFlight.fetch_add(1, std::memory_order_acq_rel);
        m_active = true;
    }
    ~CallbackGuard() { leave(); }
    explicit operator bool() const { return m_active; }
private:
    void leave()
    {
        if (!m_active || !m_context) return;
        m_active = false;
        if (m_context->inFlight.fetch_sub(1, std::memory_order_acq_rel) == 1)
            m_context->condition.notify_all();
    }
    CallbackContext *m_context = nullptr;
    bool m_active = false;
};

void __stdcall imageDataCallback(MV3D_LP_IMAGE_DATA *image, void *pUser)
{
    auto *context = static_cast<CallbackContext *>(pUser);
    CallbackGuard guard(context);
    if (!guard || !image || !context->slot) return;

    const FrameType rawType = FrameType(image->enImageType);
    if (!context->acceptsRawType(rawType)) {
        context->resetAssembly();
        context->slot->publish(makeInvalidFrame(
            *image, QStringLiteral("raw callback type 0x%1 does not match requested ImageMode")
                        .arg(std::uint32_t(rawType), 8, 16, QLatin1Char('0'))));
        return;
    }
    const detail::CallbackFrameAction action = detail::callbackFrameAction(rawType);
    if (!image->bValid) {
        context->resetAssembly();
        context->logConversion(*image, nullptr, MV3D_LP_E_ABNORMAL_IMAGE);
        context->slot->publish(makeInvalidFrame(
            *image, QStringLiteral("SDK callback frame is marked invalid (possible packet loss)")));
        return;
    }

    if (action == detail::CallbackFrameAction::CopyPointCloud) {
        if (!hasExactRasterPayload(*image, 12)) {
            context->logConversion(*image, nullptr, MV3D_LP_E_ABNORMAL_IMAGE);
            context->slot->publish(makeInvalidFrame(
                *image, QStringLiteral("SDK ABC32f payload does not match width x height x 12")));
            return;
        }
        context->logConversion(*image, image, MV3D_LP_OK);
        context->publishNormalizedFrame(copyFrame(*image));
        return;
    }

    const std::uint32_t inputBytes = action == detail::CallbackFrameAction::ConvertProfileToPointCloud
        ? 6u : action == detail::CallbackFrameAction::ConvertDepthToPointCloud ? 2u : 0u;
    if (inputBytes == 0 || !hasExactRasterPayload(*image, inputBytes)) {
        context->resetAssembly();
        context->logConversion(*image, nullptr, MV3D_LP_E_ABNORMAL_IMAGE);
        context->slot->publish(makeInvalidFrame(
            *image, QStringLiteral("unsupported or malformed SDK callback frame type 0x%1")
                        .arg(std::uint32_t(rawType), 8, 16, QLatin1Char('0'))));
        return;
    }

    // Image conversion has to run while the SDK-owned input structure is
    // alive. No device-handle API, QWidget, or project processing runs here.
    MV3D_LP_IMAGE_DATA output;
    std::memset(&output, 0, sizeof(output));
    output.enImageType = ImageType_PointCloud;
    MV3D_LP_STATUS status = MV3D_LP_E_PARAMETER;
    {
        // The vendor image-processing API has no documented reentrancy
        // contract. Serialize conversion if the SDK invokes callbacks in
        // parallel while preserving the raw frame lifetime in this scope.
        std::lock_guard<std::mutex> lock(context->conversionMutex);
        status = action == detail::CallbackFrameAction::ConvertProfileToPointCloud
            ? MV3D_LP_ImageConvert(image, &output)
            : MV3D_LP_MapDepthToPointCloud(image, &output);
    }
    if (status != MV3D_LP_OK
        || output.enImageType != ImageType_PointCloud
        || !hasExactRasterPayload(output, 12)) {
        context->logConversion(*image, &output, status);
        context->slot->publish(makeInvalidFrame(
            *image, QStringLiteral("SDK callback conversion to ABC32f failed: %1")
                        .arg(statusText(status))));
        return;
    }

    context->logConversion(*image, &output, status);
    if (rawType == FrameType::ProfileABC16) {
        context->logProfileBounds(output, image->nFrameNum);
    }
    context->publishNormalizedFrame(copyFrame(output, image));
}

void __stdcall exceptionCallback(MV3D_LP_EXCEPTION_INFO *info, void *pUser)
{
    auto *context = static_cast<CallbackContext *>(pUser);
    CallbackGuard guard(context);
    if (!guard || !context->slot) return;
    context->resetAssembly();
    auto frame = std::make_shared<CameraFrame>();
    frame->kind = FrameKind::Exception;
    frame->frameType = FrameType::Undefined;
    frame->valid = false;
    frame->timeStamp = QDateTime::currentMSecsSinceEpoch();
    if (info) {
        frame->exceptionType = std::uint32_t(info->enExceptionType);
        frame->exceptionDescription = QString::fromUtf8(
            info->chExceptionDesc,
            int(strnlen(info->chExceptionDesc, sizeof(info->chExceptionDesc))));
        if (frame->exceptionDescription.isEmpty()
            && info->enExceptionType == DevExceptionType_Disconnect)
            frame->exceptionDescription = QStringLiteral("device disconnected");
    }
    context->slot->publish(std::move(frame));
}

} // namespace

struct Mv3dLpCamera::Impl {
    explicit Impl(QString serial, std::uint32_t timeoutMs)
        : serialNumber(std::move(serial))
        , ipTimeoutMs(timeoutMs)
    {
        // 3DMVS uses Range_Image for the DP2240 production workflow. The
        // depth frame is normalized while the callback owns the SDK buffer.
        requestedMode = ImageMode::RangeImage;
    }

    QString serialNumber;
    std::uint32_t ipTimeoutMs = 3000;
    void *handle = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> deviceOpen{false};
    bool virtualDevice = false;
    LatestFrameSlot slot;
    std::optional<ImageMode> requestedMode;
    std::optional<ImageMode> activeMode;
    bool softwareTriggerRequested = false;
    bool softTriggerSupported = false;
    bool sdkLeaseHeld = false;
    CallbackContext callbackContext;
    std::mutex diagnosticsMutex;
    QString lastFrameDiagnosticKey;
    std::uint32_t matchingFrameDiagnosticCount = 0;
    QString lastFrameError;
    std::uint32_t matchingFrameErrorCount = 0;

    CameraError configureRequestedImageMode()
    {
        activeMode.reset();
        if (!requestedMode.has_value()) return {};

        MV3D_LP_PARAM setParam;
        std::memset(&setParam, 0, sizeof(setParam));
        setParam.enParamType = ParamType_Enum;
        setParam.ParamInfo.stEnumParam.nCurValue = std::uint32_t(*requestedMode);
        const MV3D_LP_STATUS setStatus = MV3D_LP_SetParam(
            handle, MV3D_LP_ENUM_IMAGEMODE, &setParam);
        if (setStatus != MV3D_LP_OK) {
            return {QStringLiteral("MV3D_LP_SetParam(ImageMode=%1) failed: %2")
                        .arg(std::uint32_t(*requestedMode))
                        .arg(statusText(setStatus))};
        }

        MV3D_LP_PARAM getParam;
        std::memset(&getParam, 0, sizeof(getParam));
        getParam.enParamType = ParamType_Enum;
        const MV3D_LP_STATUS getStatus = MV3D_LP_GetParam(
            handle, MV3D_LP_ENUM_IMAGEMODE, &getParam);
        if (getStatus != MV3D_LP_OK) {
            return {QStringLiteral("MV3D_LP_GetParam(ImageMode) failed after requesting %1: %2")
                        .arg(std::uint32_t(*requestedMode))
                        .arg(statusText(getStatus))};
        }

        activeMode = ImageMode(getParam.ParamInfo.stEnumParam.nCurValue);
        qInfo() << "Mv3dLpCamera: requested ImageMode ="
                << std::uint32_t(*requestedMode)
                << "effective ImageMode ="
                << getParam.ParamInfo.stEnumParam.nCurValue;
        if (activeMode != requestedMode) {
            return {QStringLiteral("ImageMode readback mismatch: requested %1, effective %2")
                        .arg(std::uint32_t(*requestedMode))
                        .arg(getParam.ParamInfo.stEnumParam.nCurValue)};
        }
        return {};
    }

    CameraError configureExpectedScanHeight()
    {
        MV3D_LP_PARAM param;
        std::memset(&param, 0, sizeof(param));
        param.enParamType = ParamType_Int;
        const MV3D_LP_STATUS status = MV3D_LP_GetParam(
            handle, MV3D_LP_INT_HEIGHT, &param);
        const qint64 currentHeight = qint64(param.ParamInfo.stIntParam.nCurValue);
        const qint64 maximumHeight = qint64(param.ParamInfo.stIntParam.nMax);
        const qint64 height = currentHeight > 0 ? currentHeight : maximumHeight;
        if (status != MV3D_LP_OK || height <= 0
            || height > qint64(std::numeric_limits<std::uint32_t>::max())) {
            return {QStringLiteral("MV3D_LP_GetParam(Height) failed: %1")
                        .arg(statusText(status))};
        }
        callbackContext.configureAssembly(std::uint32_t(height));
        qInfo() << "Mv3dLpCamera: configured scan Height =" << height
                << "source=" << (currentHeight > 0 ? "current" : "maximum")
                << "range=[" << param.ParamInfo.stIntParam.nMin << ","
                << param.ParamInfo.stIntParam.nMax << "]"
                << "increment=" << param.ParamInfo.stIntParam.nInc;
        return {};
    }

    CameraError configureTriggerMode()
    {
        softTriggerSupported = false;

        auto setEnum = [this](const char *key, std::uint32_t value,
                              const char *label) -> CameraError {
            MV3D_LP_PARAM param;
            std::memset(&param, 0, sizeof(param));
            param.enParamType = ParamType_Enum;
            const MV3D_LP_STATUS getStatus = MV3D_LP_GetParam(handle, key, &param);
            if (getStatus != MV3D_LP_OK) {
                return {QStringLiteral("MV3D_LP_GetParam(%1) failed: %2")
                            .arg(QString::fromLatin1(key)).arg(statusText(getStatus))};
            }
            bool supported = false;
            for (std::uint32_t i = 0; i < param.ParamInfo.stEnumParam.nSupportedNum
                                      && i < MV3D_LP_MAX_ENUM_COUNT; ++i) {
                if (param.ParamInfo.stEnumParam.nSupportValue[i] == value) {
                    supported = true;
                    break;
                }
            }
            if (!supported) {
                return {QStringLiteral("%1 value %2 is not supported by device")
                            .arg(QString::fromLatin1(label)).arg(value)};
            }
            param.ParamInfo.stEnumParam.nCurValue = value;
            const MV3D_LP_STATUS setStatus = MV3D_LP_SetParam(handle, key, &param);
            if (setStatus != MV3D_LP_OK) {
                return {QStringLiteral("MV3D_LP_SetParam(%1=%2) failed: %3")
                            .arg(QString::fromLatin1(key)).arg(value).arg(statusText(setStatus))};
            }
            MV3D_LP_PARAM verify;
            std::memset(&verify, 0, sizeof(verify));
            verify.enParamType = ParamType_Enum;
            const MV3D_LP_STATUS verifyStatus = MV3D_LP_GetParam(handle, key, &verify);
            if (verifyStatus != MV3D_LP_OK
                || verify.ParamInfo.stEnumParam.nCurValue != value) {
                return {QStringLiteral("%1 readback mismatch: requested %2, effective %3 (%4)")
                            .arg(QString::fromLatin1(label)).arg(value)
                            .arg(verify.ParamInfo.stEnumParam.nCurValue)
                            .arg(statusText(verifyStatus))};
            }
            return {};
        };

        MV3D_LP_PARAM probe;
        std::memset(&probe, 0, sizeof(probe));
        probe.enParamType = ParamType_Enum;
        const MV3D_LP_STATUS probeStatus = MV3D_LP_GetParam(
            handle, MV3D_LP_ENUM_TRIGGERMODE, &probe);
        qInfo() << "Mv3dLpCamera: TriggerMode probe status=" << statusText(probeStatus)
                << "requested=" << (softwareTriggerRequested ? "software" : "continuous");

        if (probeStatus != MV3D_LP_OK) {
            if (!softwareTriggerRequested
                && detail::isOptionalTriggerNodeStatus(std::uint32_t(probeStatus))) {
                qWarning().noquote()
                    << "Mv3dLpCamera: TriggerMode is unavailable; continuing with SDK default continuous acquisition"
                    << statusText(probeStatus);
                qInfo() << "Mv3dLpCamera: trigger configuration compatibility="
                        << "ignored optional TriggerMode failure";
                return {};
            }
            if (softwareTriggerRequested
                && detail::isOptionalTriggerNodeStatus(std::uint32_t(probeStatus))) {
                return {QStringLiteral("device does not support software trigger: TriggerMode probe failed: %1")
                            .arg(statusText(probeStatus))};
            }
            return {QStringLiteral("MV3D_LP_GetParam(TriggerMode) failed: %1")
                        .arg(statusText(probeStatus))};
        }

        const std::uint32_t requestedValue = softwareTriggerRequested ? 1u : 0u;
        bool supported = false;
        for (std::uint32_t i = 0; i < probe.ParamInfo.stEnumParam.nSupportedNum
                                  && i < MV3D_LP_MAX_ENUM_COUNT; ++i) {
            if (probe.ParamInfo.stEnumParam.nSupportValue[i] == requestedValue) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            if (softwareTriggerRequested) {
                return {QStringLiteral("device does not support software trigger: TriggerMode value %1 is unavailable")
                            .arg(requestedValue)};
            }
            qWarning() << "Mv3dLpCamera: TriggerMode=0 is unavailable; continuing with SDK default";
            return {};
        }

        const CameraError modeError = setEnum(
            MV3D_LP_ENUM_TRIGGERMODE, requestedValue, "TriggerMode");
        if (!modeError.ok()) {
            if (softwareTriggerRequested) return modeError;
            qWarning().noquote() << "Mv3dLpCamera: optional continuous TriggerMode configuration failed;"
                                 << modeError.message;
            return {};
        }
        if (softwareTriggerRequested) {
            const CameraError sourceError = setEnum(
                MV3D_LP_ENUM_TRIGGERSOURCE, 7u, "TriggerSource(Software)");
            if (!sourceError.ok()) {
                return {QStringLiteral("device does not support software trigger: %1")
                            .arg(sourceError.message)};
            }
            softTriggerSupported = true;
        }
        qInfo() << "Mv3dLpCamera: trigger mode"
                << (softwareTriggerRequested ? "software" : "continuous");
        return {};
    }

    void logFrameDiagnostic(const CameraFrame &frame)
    {
        const QString key = QStringLiteral("request=%1 effective=%2 type=0x%3 valid=%4")
            .arg(requestedMode.has_value() ? QString::number(std::uint32_t(*requestedMode))
                                            : QStringLiteral("none"))
            .arg(activeMode.has_value() ? QString::number(std::uint32_t(*activeMode))
                                         : QStringLiteral("unknown"))
            .arg(std::uint32_t(frame.frameType), 8, 16, QLatin1Char('0'))
            .arg(frame.valid);

        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        if (key == lastFrameDiagnosticKey) {
            ++matchingFrameDiagnosticCount;
        } else {
            lastFrameDiagnosticKey = key;
            matchingFrameDiagnosticCount = 1;
        }
        if (matchingFrameDiagnosticCount == 1 || matchingFrameDiagnosticCount % 120 == 0) {
            qInfo().noquote() << "Mv3dLpCamera: frame diagnostic"
                              << key
                              << "size=" << frame.width << "x" << frame.height
                              << "len=" << frame.data.size()
                              << "scale=[" << frame.xScale << frame.yScale << frame.zScale << "]"
                              << "offset=[" << frame.xOffset << frame.yOffset << frame.zOffset << "]"
                              << "scanRows=" << frame.accumulatedScanRows << "/"
                              << frame.expectedScanRows
                              << "scanFragments=" << frame.scanFragmentCount
                              << "discardedScans=" << frame.discardedScanCount
                              << "count=" << matchingFrameDiagnosticCount;
        }
    }

    void logFrameError(const QString &error)
    {
        std::lock_guard<std::mutex> lock(diagnosticsMutex);
        if (error == lastFrameError) {
            ++matchingFrameErrorCount;
        } else {
            lastFrameError = error;
            matchingFrameErrorCount = 1;
        }
        if (matchingFrameErrorCount == 1 || matchingFrameErrorCount % 120 == 0) {
            qWarning().noquote() << "Mv3dLpCamera:" << error
                                 << "count=" << matchingFrameErrorCount;
        }
    }

    void waitForCallbacks()
    {
        callbackContext.accepting.store(false, std::memory_order_release);
        std::unique_lock<std::mutex> lock(callbackContext.mutex);
        callbackContext.condition.wait(lock, [this]() {
            return callbackContext.inFlight.load(std::memory_order_acquire) == 0;
        });
    }

    void shutdown()
    {
        slot.clearFramePublishedCallback();
        waitForCallbacks();
        callbackContext.slot = nullptr;
        callbackContext.resetAssembly();
        if (running.load() && handle != nullptr) {
            MV3D_LP_StopMeasure(handle);
        }
        running.store(false);
        if (deviceOpen.load() || handle != nullptr) {
            MV3D_LP_CloseDevice(&handle);
            deviceOpen.store(false);
            handle = nullptr;
        }
        if (sdkLeaseHeld) {
            releaseSdk();
            sdkLeaseHeld = false;
        }
    }
};

Mv3dLpCamera::Mv3dLpCamera(const QString &serialNumber, std::uint32_t ipTimeoutMs)
    : m_impl(std::make_unique<Impl>(serialNumber, ipTimeoutMs))
{
    m_impl->virtualDevice = serialNumber.startsWith(QStringLiteral("Vir"));
}

Mv3dLpCamera::~Mv3dLpCamera()
{
    stop();
}

QVector<DeviceInfo> Mv3dLpCamera::enumerateDevices(CameraError *error)
{
    QVector<DeviceInfo> devices;
    if (!ensureSdkInitialized(error)) return devices;

    std::uint32_t deviceNumber = 0;
    if (MV3D_LP_GetDeviceNumber(&deviceNumber) != MV3D_LP_OK) {
        if (error) error->message = QStringLiteral("MV3D_LP_GetDeviceNumber failed");
        releaseSdk();
        return devices;
    }
    if (deviceNumber == 0) {
        releaseSdk();
        return devices;
    }
    QVector<MV3D_LP_DEVICE_INFO> infos(deviceNumber);
    std::uint32_t filled = 0;
    if (MV3D_LP_GetDeviceList(infos.data(), deviceNumber, &filled) != MV3D_LP_OK) {
        if (error) error->message = QStringLiteral("MV3D_LP_GetDeviceList failed");
        releaseSdk();
        return devices;
    }
    devices.reserve(filled);
    for (std::uint32_t i = 0; i < filled; ++i) {
        DeviceInfo info;
        info.modelName = QString::fromUtf8(infos[i].chModelName,
                                           int(strnlen(infos[i].chModelName, sizeof(infos[i].chModelName))));
        info.serialNumber = QString::fromUtf8(infos[i].chSerialNumber,
                                              int(strnlen(infos[i].chSerialNumber, sizeof(infos[i].chSerialNumber))));
        info.currentIp = QString::fromUtf8(infos[i].chCurrentIp,
                                           int(strnlen(infos[i].chCurrentIp, sizeof(infos[i].chCurrentIp))));
        info.userDefinedName = QString::fromUtf8(infos[i].chUserDefinedName,
                                                 int(strnlen(infos[i].chUserDefinedName, sizeof(infos[i].chUserDefinedName))));
        // 3DMVS virtual cameras report transport layer type 16 and an SN
        // prefixed with "Vir"; they support the same acquisition flow.
        info.isVirtual = (infos[i].nDevTypeInfo & 0xFF000000u) == 0x10000000u
            || info.serialNumber.startsWith(QStringLiteral("Vir"));
        devices.push_back(std::move(info));
    }
    releaseSdk();
    return devices;
}

CameraError Mv3dLpCamera::start()
{
    if (m_impl->running.load()) return {};
    if (!m_impl->sdkLeaseHeld && !ensureSdkInitialized(nullptr)) {
        return {QStringLiteral("MV3D_LP_Initialize failed")};
    }
    m_impl->sdkLeaseHeld = true;

    const QByteArray sn = m_impl->serialNumber.toUtf8();
    if (MV3D_LP_OpenDeviceBySN(&m_impl->handle, sn.constData()) != MV3D_LP_OK) {
        m_impl->shutdown();
        return {QStringLiteral("MV3D_LP_OpenDeviceBySN failed for %1")
                    .arg(m_impl->serialNumber)};
    }
    m_impl->deviceOpen.store(true);

    const CameraError modeError = m_impl->configureRequestedImageMode();
    if (!modeError.ok()) {
        m_impl->shutdown();
        return modeError;
    }

    const CameraError heightError = m_impl->configureExpectedScanHeight();
    if (!heightError.ok()) {
        m_impl->shutdown();
        return heightError;
    }
    const CameraError triggerError = m_impl->configureTriggerMode();
    if (!triggerError.ok()) {
        m_impl->shutdown();
        return triggerError;
    }
    m_impl->callbackContext.configureActiveImageMode(m_impl->activeMode);

    // Register callbacks before measurement starts.
    m_impl->callbackContext.slot = &m_impl->slot;
    m_impl->callbackContext.accepting.store(true, std::memory_order_release);
    const auto imageCallbackStatus = MV3D_LP_RegisterImageDataCallBack(
        m_impl->handle, imageDataCallback, &m_impl->callbackContext);
    const auto exceptionCallbackStatus = MV3D_LP_RegisterExceptionCallBack(
        m_impl->handle, exceptionCallback, &m_impl->callbackContext);
    if (imageCallbackStatus != MV3D_LP_OK || exceptionCallbackStatus != MV3D_LP_OK) {
        m_impl->shutdown();
        return {QStringLiteral("Mv3dLp callback registration failed")};
    }

    const MV3D_LP_STATUS startStatus = MV3D_LP_StartMeasure(m_impl->handle);
    if (startStatus != MV3D_LP_OK) {
        m_impl->shutdown();
        return {QStringLiteral("MV3D_LP_StartMeasure failed: %1").arg(statusText(startStatus))};
    }
    qInfo() << "Mv3dLpCamera: MV3D_LP_StartMeasure succeeded"
            << "trigger=" << (m_impl->softwareTriggerRequested ? "software" : "continuous")
            << "softTriggerSupported=" << m_impl->softTriggerSupported;
    m_impl->running.store(true);
    return {};
}

void Mv3dLpCamera::stop()
{
    // Synchronized with publish(), so teardown cannot queue a new UI call.
    m_impl->slot.clearFramePublishedCallback();
    m_impl->shutdown();
}

bool Mv3dLpCamera::isRunning() const
{
    return m_impl->running.load();
}

bool Mv3dLpCamera::supportsSoftTrigger() const
{
    return m_impl->softTriggerSupported;
}

QString Mv3dLpCamera::deviceSummary() const
{
    return QStringLiteral("Mv3dLp camera %1").arg(m_impl->serialNumber);
}

LatestFrameSlot &Mv3dLpCamera::frameSlot()
{
    return m_impl->slot;
}

CameraError Mv3dLpCamera::softTrigger()
{
    if (!m_impl->running.load()) {
        return {QStringLiteral("soft trigger requires streaming to be running")};
    }
    if (!m_impl->softTriggerSupported) {
        return {QStringLiteral("device does not support software trigger")};
    }
    const MV3D_LP_STATUS status = MV3D_LP_SoftTrigger(m_impl->handle);
    if (status != MV3D_LP_OK) {
        return {QStringLiteral("MV3D_LP_SoftTrigger failed: %1").arg(statusText(status))};
    }
    return {};
}

CameraError Mv3dLpCamera::reconnectOnce()
{
    // Reuse the same cleanup and strict ImageMode negotiation as start().
    m_impl->slot.clearFramePublishedCallback();
    m_impl->shutdown();
    const CameraError error = start();
    if (error.ok()) return {};
    return {QStringLiteral("reconnect failed: %1").arg(error.message)};
}

bool Mv3dLpCamera::isVirtualDevice() const
{
    return m_impl->virtualDevice;
}

void Mv3dLpCamera::requestImageMode(ImageMode mode)
{
    m_impl->requestedMode = mode;
}

void Mv3dLpCamera::requestSoftwareTrigger(bool enabled)
{
    m_impl->softwareTriggerRequested = enabled;
}

std::optional<ImageMode> Mv3dLpCamera::activeImageMode() const
{
    return m_impl->activeMode;
}

PointCloudConvertResult Mv3dLpCamera::convertFrame(
    const CameraFrame &frame, const PointCloudConvertOptions &options)
{
    PointCloudConvertResult rejected;
    m_impl->logFrameDiagnostic(frame);
    if (frame.kind != FrameKind::Frame) {
        rejected.error = QStringLiteral("camera event is not a convertible image frame");
        return rejected;
    }
    if (!frame.valid) {
        rejected.error = frame.exceptionDescription.isEmpty()
            ? QStringLiteral("frame is marked invalid (possible packet loss)")
            : frame.exceptionDescription;
        m_impl->logFrameError(rejected.error);
        return rejected;
    }
    const QString modeError = detail::imageModeFrameTypeError(
        m_impl->requestedMode, m_impl->activeMode, frame.frameType);
    if (!modeError.isEmpty()) {
        rejected.error = modeError;
        m_impl->logFrameError(rejected.error);
        return rejected;
    }
    if (frame.frameType == FrameType::PointCloudABC32f) {
        return convertPointCloudToPoints(frame, options);
    }

    rejected.error = QStringLiteral(
        "live SDK frame was not normalized to ABC32f in the callback");
    m_impl->logFrameError(rejected.error);
    return rejected;
}

} // namespace pcv::camera
