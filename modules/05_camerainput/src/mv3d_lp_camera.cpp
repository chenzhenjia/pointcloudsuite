#include <pcv/camera/mv3d_lp_camera.h>

#include <pcv/camera/frame_converter.h>
#include <pcv/camera/frame_data.h>

#include <QDateTime>
#include <QDebug>
#include <QString>

#include <Mv3dLpApi.h>
#include <Mv3dLpDefine.h>
#include <Mv3dLpImgProc.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
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
    return QStringLiteral("0x%1").arg(quint32(status), 8, 16, QLatin1Char('0'));
}

// Owned frame copy performed on the SDK callback thread.
std::shared_ptr<CameraFrame> copyFrame(const MV3D_LP_IMAGE_DATA &image)
{
    auto frame = std::make_shared<CameraFrame>();
    frame->frameType = FrameType(image.enImageType);
    frame->width = image.nWidth;
    frame->height = image.nHeight;
    frame->frameNumber = image.nFrameNum;
    frame->timeStamp = qint64(image.nTimeStamp);
    frame->valid = image.bValid != 0;
    frame->xScale = image.fXScale;
    frame->yScale = image.fYScale;
    frame->zScale = image.fZScale;
    frame->xOffset = image.nXOffset;
    frame->yOffset = image.nYOffset;
    frame->zOffset = image.nZOffset;
    if (image.pData && image.nDataLen > 0) {
        frame->data = QByteArray(reinterpret_cast<const char *>(image.pData),
                                 qsizetype(image.nDataLen));
    }
    if (image.pIntensityData && image.nIntensityDataLen > 0) {
        frame->intensityData = QByteArray(
            reinterpret_cast<const char *>(image.pIntensityData),
            qsizetype(image.nIntensityDataLen));
    }
    return frame;
}

struct CallbackContext {
    std::atomic<bool> accepting{false};
    std::atomic<int> inFlight{0};
    std::mutex mutex;
    std::condition_variable condition;
    LatestFrameSlot *slot = nullptr;
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
    context->slot->publish(copyFrame(*image));
}

void __stdcall exceptionCallback(MV3D_LP_EXCEPTION_INFO *info, void *pUser)
{
    auto *context = static_cast<CallbackContext *>(pUser);
    CallbackGuard guard(context);
    if (!guard || !context->slot) return;
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
        // Default to the 3DMVS workflow for DP-series sensors: depth image
        // (Range_Image) output, converted to point cloud by the SDK.
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
    bool sdkLeaseHeld = false;
    CallbackContext callbackContext;
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
        releaseSdk();
        m_impl->sdkLeaseHeld = false;
        return {QStringLiteral("MV3D_LP_OpenDeviceBySN failed for %1")
                    .arg(m_impl->serialNumber)};
    }
    m_impl->deviceOpen.store(true);

    // The DP2240 primary workflow asks for Range_Image. The converter still
    // dispatches by actual enImageType because HFR devices can report a
    // different effective mode than the requested one.
    if (m_impl->requestedMode.has_value()) {
        MV3D_LP_PARAM param;
        std::memset(&param, 0, sizeof(param));
        param.enParamType = ParamType_Enum;
        param.ParamInfo.stEnumParam.nCurValue =
            std::uint32_t(*m_impl->requestedMode);
        if (MV3D_LP_SetParam(m_impl->handle, MV3D_LP_ENUM_IMAGEMODE, &param) != MV3D_LP_OK) {
            qWarning() << "Mv3dLpCamera: SetParam ImageMode failed for mode"
                       << std::uint32_t(*m_impl->requestedMode);
        }
    }
    {
        // Read the effective mode for diagnostics; failures are not fatal.
        MV3D_LP_PARAM param;
        std::memset(&param, 0, sizeof(param));
        param.enParamType = ParamType_Enum;
        if (MV3D_LP_GetParam(m_impl->handle, MV3D_LP_ENUM_IMAGEMODE, &param) == MV3D_LP_OK) {
            m_impl->activeMode = ImageMode(param.ParamInfo.stEnumParam.nCurValue);
            qInfo() << "Mv3dLpCamera: effective ImageMode ="
                    << std::uint32_t(param.ParamInfo.stEnumParam.nCurValue);
        } else {
            qWarning() << "Mv3dLpCamera: GetParam(ImageMode) failed";
        }
    }

    // Register callbacks before measurement starts.
    m_impl->callbackContext.slot = &m_impl->slot;
    m_impl->callbackContext.accepting.store(true, std::memory_order_release);
    const auto imageCallbackStatus = MV3D_LP_RegisterImageDataCallBack(
        m_impl->handle, imageDataCallback, &m_impl->callbackContext);
    const auto exceptionCallbackStatus = MV3D_LP_RegisterExceptionCallBack(
        m_impl->handle, exceptionCallback, &m_impl->callbackContext);
    if (imageCallbackStatus != MV3D_LP_OK || exceptionCallbackStatus != MV3D_LP_OK) {
        m_impl->callbackContext.accepting.store(false, std::memory_order_release);
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        releaseSdk();
        m_impl->sdkLeaseHeld = false;
        return {QStringLiteral("Mv3dLp callback registration failed")};
    }

    const MV3D_LP_STATUS startStatus = MV3D_LP_StartMeasure(m_impl->handle);
    if (startStatus != MV3D_LP_OK) {
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        releaseSdk();
        m_impl->sdkLeaseHeld = false;
        return {QStringLiteral("MV3D_LP_StartMeasure failed: %1").arg(statusText(startStatus))};
    }
    m_impl->running.store(true);
    return {};
}

void Mv3dLpCamera::stop()
{
    // Synchronized with publish(), so teardown cannot queue a new UI call.
    m_impl->slot.clearFramePublishedCallback();
    m_impl->callbackContext.accepting.store(false, std::memory_order_release);
    {
        std::unique_lock<std::mutex> lock(m_impl->callbackContext.mutex);
        m_impl->callbackContext.condition.wait(lock, [this]() {
            return m_impl->callbackContext.inFlight.load(std::memory_order_acquire) == 0;
        });
    }
    if (m_impl->running.load()) {
        MV3D_LP_StopMeasure(m_impl->handle);
        m_impl->running.store(false);
    }
    if (m_impl->deviceOpen.load()) {
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        m_impl->handle = nullptr;
    }
    if (m_impl->sdkLeaseHeld) {
        releaseSdk();
        m_impl->sdkLeaseHeld = false;
    }
}

bool Mv3dLpCamera::isRunning() const
{
    return m_impl->running.load();
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
    const MV3D_LP_STATUS status = MV3D_LP_SoftTrigger(m_impl->handle);
    if (status != MV3D_LP_OK) {
        return {QStringLiteral("MV3D_LP_SoftTrigger failed: %1").arg(statusText(status))};
    }
    return {};
}

CameraError Mv3dLpCamera::reconnectOnce()
{
    // Stop whatever is still active, then reopen and restart streaming.
    m_impl->callbackContext.accepting.store(false, std::memory_order_release);
    {
        std::unique_lock<std::mutex> lock(m_impl->callbackContext.mutex);
        m_impl->callbackContext.condition.wait(lock, [this]() {
            return m_impl->callbackContext.inFlight.load(std::memory_order_acquire) == 0;
        });
    }
    if (m_impl->running.load()) {
        MV3D_LP_StopMeasure(m_impl->handle);
        m_impl->running.store(false);
    }
    if (m_impl->deviceOpen.load()) {
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        m_impl->handle = nullptr;
    }
    if (!m_impl->sdkLeaseHeld && !ensureSdkInitialized(nullptr)) {
        return {QStringLiteral("MV3D_LP_Initialize failed during reconnect")};
    }
    m_impl->sdkLeaseHeld = true;
    const QByteArray sn = m_impl->serialNumber.toUtf8();
    if (MV3D_LP_OpenDeviceBySN(&m_impl->handle, sn.constData()) != MV3D_LP_OK) {
        if (m_impl->sdkLeaseHeld) { releaseSdk(); m_impl->sdkLeaseHeld = false; }
        return {QStringLiteral("MV3D_LP_OpenDeviceBySN failed during reconnect")};
    }
    m_impl->deviceOpen.store(true);
    if (m_impl->requestedMode.has_value()) {
        MV3D_LP_PARAM param;
        std::memset(&param, 0, sizeof(param));
        param.enParamType = ParamType_Enum;
        param.ParamInfo.stEnumParam.nCurValue = std::uint32_t(*m_impl->requestedMode);
        MV3D_LP_SetParam(m_impl->handle, MV3D_LP_ENUM_IMAGEMODE, &param);
    }
    m_impl->callbackContext.slot = &m_impl->slot;
    m_impl->callbackContext.accepting.store(true, std::memory_order_release);
    const auto imageCallbackStatus = MV3D_LP_RegisterImageDataCallBack(
        m_impl->handle, imageDataCallback, &m_impl->callbackContext);
    const auto exceptionCallbackStatus = MV3D_LP_RegisterExceptionCallBack(
        m_impl->handle, exceptionCallback, &m_impl->callbackContext);
    if (imageCallbackStatus != MV3D_LP_OK || exceptionCallbackStatus != MV3D_LP_OK) {
        m_impl->callbackContext.accepting.store(false, std::memory_order_release);
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        if (m_impl->sdkLeaseHeld) { releaseSdk(); m_impl->sdkLeaseHeld = false; }
        return {QStringLiteral("Mv3dLp callback registration failed during reconnect")};
    }
    const MV3D_LP_STATUS startStatus = MV3D_LP_StartMeasure(m_impl->handle);
    if (startStatus != MV3D_LP_OK) {
        MV3D_LP_CloseDevice(&m_impl->handle);
        m_impl->deviceOpen.store(false);
        if (m_impl->sdkLeaseHeld) { releaseSdk(); m_impl->sdkLeaseHeld = false; }
        return {QStringLiteral("MV3D_LP_StartMeasure failed during reconnect: %1")
                    .arg(statusText(startStatus))};
    }
    m_impl->running.store(true);
    return {};
}

bool Mv3dLpCamera::isVirtualDevice() const
{
    return m_impl->virtualDevice;
}

void Mv3dLpCamera::requestImageMode(ImageMode mode)
{
    m_impl->requestedMode = mode;
}

std::optional<ImageMode> Mv3dLpCamera::activeImageMode() const
{
    return m_impl->activeMode;
}

PointCloudConvertResult Mv3dLpCamera::convertFrame(
    const CameraFrame &frame, const PointCloudConvertOptions &options)
{
    PointCloudConvertResult rejected;
    if (frame.kind != FrameKind::Frame) {
        rejected.error = QStringLiteral("camera event is not a convertible image frame");
        return rejected;
    }
    if (!frame.valid) {
        rejected.error = QStringLiteral("frame is marked invalid (possible packet loss)");
        return rejected;
    }
    if (frame.frameType == FrameType::PointCloudABC32f) {
        return convertPointCloudToPoints(frame, options);
    }

    // Depth and profile frames are converted by the SDK when available so the
    // metric coordinates match the vendor calibration.
    if (frame.frameType == FrameType::DepthC16
        || frame.frameType == FrameType::ProfileABC16) {
        MV3D_LP_IMAGE_DATA in;
        std::memset(&in, 0, sizeof(in));
        in.enImageType = static_cast<Mv3dLpImageType>(std::uint32_t(frame.frameType));
        in.nWidth = frame.width;
        in.nHeight = frame.height;
        in.nDataLen = std::uint32_t(frame.data.size());
        in.pData = reinterpret_cast<quint8 *>(const_cast<char *>(frame.data.constData()));
        in.nFrameNum = frame.frameNumber;
        in.nTimeStamp = qint64(frame.timeStamp);
        in.bValid = frame.valid ? 1 : 0;
        in.fXScale = frame.xScale;
        in.fYScale = frame.yScale;
        in.fZScale = frame.zScale;
        in.nXOffset = frame.xOffset;
        in.nYOffset = frame.yOffset;
        in.nZOffset = frame.zOffset;

        MV3D_LP_IMAGE_DATA out;
        std::memset(&out, 0, sizeof(out));

        MV3D_LP_STATUS status = MV3D_LP_E_PARAMETER;
        if (frame.frameType == FrameType::DepthC16) {
            status = MV3D_LP_MapDepthToPointCloud(&in, &out);
        } else {
            out.enImageType = ImageType_PointCloud;
            status = MV3D_LP_ImageConvert(&in, &out);
        }
        if (status == MV3D_LP_OK && out.pData && out.nDataLen > 0) {
            CameraFrame pointCloudFrame;
            pointCloudFrame.frameType = FrameType::PointCloudABC32f;
            pointCloudFrame.width = out.nWidth;
            pointCloudFrame.height = out.nHeight;
            pointCloudFrame.valid = true;
            pointCloudFrame.data = QByteArray(
                reinterpret_cast<const char *>(out.pData), qsizetype(out.nDataLen));
            return convertPointCloudToPoints(pointCloudFrame, options);
        }
        rejected.error = QStringLiteral("Mv3dLp SDK conversion failed for frame type 0x%1: %2")
            .arg(std::uint32_t(frame.frameType), 8, 16, QLatin1Char('0'))
            .arg(statusText(status));
        qWarning().noquote() << rejected.error;
        return rejected;
    }

    rejected.error = QStringLiteral("unsupported frame type for live SDK conversion");
    return rejected;
}

} // namespace pcv::camera
