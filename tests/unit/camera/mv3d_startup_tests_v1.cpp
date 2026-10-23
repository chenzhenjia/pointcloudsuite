#include <pcv/camera/mv3d_lp_camera.h>
#include <pcv/camera/capture_session_v1.h>
#include <Mv3dLpApi.h>
#include <Mv3dLpImgProc.h>

#include <QCoreApplication>
#include <QStringList>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>

// This executable compiles the production driver against the vendor's real
// declarations, but defines every used SDK entry point here. No device/DLL is
// opened. An idle ClearDataBuffer deliberately reproduces the field failure.
#define CHECK(c) do { if (!(c)) { std::cerr << "line " << __LINE__ << ": " #c "\n"; return 1; } } while (false)
namespace {
struct FakeSdk {
    QStringList calls;
    QString failStage;
    MV3D_LP_STATUS failure = MV3D_LP_STATUS(MV3D_LP_E_RESOURCE);
    bool opened = false, measuring = false, emitDuringStartup = true, exceptionDuringClear = false;
    int leases = 0, starts = 0, stops = 0, closes = 0;
    uint32_t mode = uint32_t(pcv::camera::ImageMode::PointCloudImage);
    MV3D_LP_ImageDataCallBack image = nullptr;
    MV3D_LP_ExceptionCallBack exception = nullptr;
    void *imageUser = nullptr, *exceptionUser = nullptr;
    MV3D_LP_STATUS call(const char *name) {
        calls.push_back(QString::fromLatin1(name));
        return failStage == QLatin1String(name) ? failure : MV3D_LP_OK;
    }
    void emitImage(uint32_t number = 0) {
        if (!image) return;
        float points[] = {1, 0, 1, 2, 0, 1, 1, 0, 2, 2, 0, 2};
        uint8_t intensity[] = {100, 110, 120, 130};
        MV3D_LP_IMAGE_DATA data{};
        data.enImageType = Mv3dLpImageType(uint32_t(pcv::camera::FrameType::PointCloudABC32f));
        data.bValid = true; data.nWidth = 2; data.nHeight = 2;
        data.nFrameNum = number; data.nDataLen = sizeof(points);
        data.pData = reinterpret_cast<uint8_t *>(points);
        data.pIntensityData = intensity; data.nIntensityDataLen = sizeof(intensity);
        image(&data, imageUser);
    }
} sdk;
pcv::camera::CaptureConfiguration configuration() {
    pcv::camera::CaptureConfiguration c;
    c.boundaryMode = pcv::camera::CaptureBoundaryMode::SdkImageFrame;
    c.sdkImageBoundaryVerified = true;
    return c;
}
void selectMode(pcv::camera::Mv3dLpCamera &camera) {
    camera.requestImageMode(pcv::camera::ImageMode::PointCloudImage);
}
}

extern "C" {
MV3D_LP_STATUS MV3D_LP_Initialize() { ++sdk.leases; return sdk.call("Initialize"); }
MV3D_LP_STATUS MV3D_LP_Finalize() { --sdk.leases; return sdk.call("Finalize"); }
MV3D_LP_STATUS MV3D_LP_GetDeviceNumber(uint32_t *count) { *count = 0; return MV3D_LP_OK; }
MV3D_LP_STATUS MV3D_LP_GetDeviceList(MV3D_LP_DEVICE_INFO *, uint32_t, uint32_t *count) { *count = 0; return MV3D_LP_OK; }
MV3D_LP_STATUS MV3D_LP_OpenDeviceBySN(HANDLE *handle, const char *) {
    const auto status = sdk.call("Open");
    if (status == MV3D_LP_OK) { sdk.opened = true; *handle = &sdk; }
    return status;
}
MV3D_LP_STATUS MV3D_LP_CloseDevice(HANDLE *handle) {
    ++sdk.closes; sdk.opened = false; *handle = nullptr; return sdk.call("Close");
}
MV3D_LP_STATUS MV3D_LP_SetParam(HANDLE, const char *key, MV3D_LP_PARAM *param) {
    if (std::strcmp(key, MV3D_LP_ENUM_IMAGEMODE) != 0) return MV3D_LP_E_PARAMETER;
    sdk.mode = param->ParamInfo.stEnumParam.nCurValue;
    return sdk.call("SetImageMode");
}
MV3D_LP_STATUS MV3D_LP_GetParam(HANDLE, const char *key, MV3D_LP_PARAM *param) {
    if (std::strcmp(key, MV3D_LP_ENUM_IMAGEMODE) == 0) {
        param->ParamInfo.stEnumParam.nCurValue = sdk.mode; return sdk.call("ReadImageMode");
    }
    return MV3D_LP_E_PARAMETER; // Optional Height/trigger nodes on the field device.
}
MV3D_LP_STATUS MV3D_LP_RegisterImageDataCallBack(HANDLE, MV3D_LP_ImageDataCallBack cb, void *user) {
    sdk.image = cb; sdk.imageUser = user;
    if (sdk.emitDuringStartup) sdk.emitImage();
    return sdk.call("RegisterImage");
}
MV3D_LP_STATUS MV3D_LP_RegisterExceptionCallBack(HANDLE, MV3D_LP_ExceptionCallBack cb, void *user) {
    sdk.exception = cb; sdk.exceptionUser = user; return sdk.call("RegisterException");
}
MV3D_LP_STATUS MV3D_LP_StartMeasure(HANDLE) {
    ++sdk.starts;
    const auto status = sdk.call("Start");
    if (sdk.emitDuringStartup) sdk.emitImage();
    if (status == MV3D_LP_OK) sdk.measuring = true;
    return status;
}
MV3D_LP_STATUS MV3D_LP_ClearDataBuffer(HANDLE) {
    const auto status = sdk.call("Clear");
    if (!sdk.measuring) return MV3D_LP_E_CALLORDER;
    if (sdk.emitDuringStartup) sdk.emitImage();
    if (sdk.exceptionDuringClear) {
        MV3D_LP_EXCEPTION_INFO info{};
        info.enExceptionType = DevExceptionType_Disconnect;
        sdk.exception(&info, sdk.exceptionUser);
    }
    return status;
}
MV3D_LP_STATUS MV3D_LP_StopMeasure(HANDLE) {
    ++sdk.stops; sdk.measuring = false; sdk.emitImage(93); return sdk.call("Stop");
}
MV3D_LP_STATUS MV3D_LP_SoftTrigger(HANDLE) { return MV3D_LP_E_SUPPORT; }
MV3D_LP_STATUS MV3D_LP_MapDepthToPointCloud(MV3D_LP_IMAGE_DATA *, MV3D_LP_IMAGE_DATA *) { return MV3D_LP_E_SUPPORT; }
MV3D_LP_STATUS MV3D_LP_ImageConvert(MV3D_LP_IMAGE_DATA *, MV3D_LP_IMAGE_DATA *) { return MV3D_LP_E_SUPPORT; }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using pcv::camera::Mv3dLpCamera;
    using pcv::camera::CaptureFrameQueue;
    // Actual production start(), including optional trigger nodes and frame queue.
    sdk = {};
    {
        Mv3dLpCamera camera(QStringLiteral("TestCamera")); selectMode(camera);
        CHECK(camera.configureCapture(configuration()).ok());
        auto queue = std::make_shared<CaptureFrameQueue>(configuration());
        camera.frameSlot().setCaptureQueue(queue);
        int notifications = 0;
        camera.frameSlot().setFramePublishedCallback([&] { ++notifications; });
        CHECK(camera.start().ok() && camera.isRunning() && sdk.measuring);
        CHECK(sdk.calls == QStringList({"Initialize", "Open", "SetImageMode", "ReadImageMode",
            "RegisterImage", "RegisterException", "Start", "Clear"}));
        CHECK(queue->received() == 0 && notifications == 0 && !camera.frameSlot().take());
        sdk.emitImage(100);
        CHECK(queue->received() == 1 && notifications == 1);
        const auto frame = camera.frameSlot().take();
        CHECK(frame && frame->frameNumber == 100 && frame->completeScan && frame->height == 2);
        CHECK(frame->intensityWidth == 2 && frame->intensityHeight == 2
              && frame->intensityData.size() == 4 && quint8(frame->intensityData.at(0)) == 100);
        CHECK(camera.start().ok() && sdk.starts == 1); // Idempotent start.
        camera.stop(); camera.stop();
        sdk.emitImage(101);
        CHECK(!camera.isRunning() && !sdk.measuring && !sdk.opened && sdk.leases == 0);
        CHECK(sdk.stops == 1 && sdk.closes == 1 && queue->received() == 1);
        CHECK(sdk.calls.mid(sdk.calls.size() - 3) == QStringList({"Stop", "Close", "Finalize"}));
        for (int repeat = 0; repeat < 3; ++repeat) {
            CHECK(camera.start().ok());
            CHECK(!camera.frameSlot().take());
            sdk.emitImage(102 + repeat); CHECK(camera.frameSlot().take());
            camera.stop(); CHECK(sdk.leases == 0 && !sdk.measuring);
        }
    }
    // Failure at every startup stage must release leases/handles, and stop
    // measurement iff Start succeeded. Failed clear is never silently ignored.
    for (const auto &stage : {"Open", "SetImageMode", "RegisterImage", "RegisterException", "Start", "Clear"}) {
        sdk = {}; sdk.failStage = QString::fromLatin1(stage);
        sdk.failure = sdk.failStage == "Open" ? MV3D_LP_E_ACCESS_DENIED : MV3D_LP_E_CALLORDER;
        Mv3dLpCamera camera(QStringLiteral("TestCamera")); selectMode(camera);
        CHECK(camera.configureCapture(configuration()).ok());
        auto queue = std::make_shared<CaptureFrameQueue>(configuration());
        camera.frameSlot().setCaptureQueue(queue);
        const auto error = camera.start();
        CHECK(!error.ok() && !camera.isRunning() && !sdk.measuring && !sdk.opened && sdk.leases == 0);
        CHECK(!camera.frameSlot().take() && queue->received() == 0);
        CHECK(sdk.stops == (sdk.failStage == "Clear" ? 1 : 0));
        CHECK(sdk.closes == (sdk.failStage == "Open" ? 0 : 1));
        if (sdk.failStage == "Open") CHECK(error.message.contains("MV3D_LP_E_ACCESS_DENIED") && error.message.contains("0x8006000e"));
        if (sdk.failStage == "Clear") CHECK(error.message.contains("MV3D_LP_ClearDataBuffer") && error.message.contains("MV3D_LP_E_CALLORDER"));
        sdk.failStage.clear();
        CHECK(camera.start().ok()); camera.stop(); CHECK(sdk.leases == 0);
    }
    // Exceptions are not hidden by the startup image gate.
    sdk = {}; sdk.exceptionDuringClear = true;
    {
        Mv3dLpCamera camera(QStringLiteral("TestCamera")); selectMode(camera);
        CHECK(camera.configureCapture(configuration()).ok());
        auto queue = std::make_shared<CaptureFrameQueue>(configuration());
        camera.frameSlot().setCaptureQueue(queue);
        CHECK(camera.start().ok());
        const auto event = camera.frameSlot().take();
        CHECK(event && event->kind == pcv::camera::FrameKind::Exception && !queue->error().isEmpty());
        camera.stop(); CHECK(sdk.leases == 0);
    }
    // Legacy preview/fixed rows do not clear and still accept start callbacks.
    for (const unsigned rows : {0u, 2u}) {
        sdk = {};
        Mv3dLpCamera camera(QStringLiteral("TestCamera")); selectMode(camera);
        CHECK(camera.configureCaptureRows(rows).ok());
        CHECK(camera.start().ok() && !sdk.calls.contains("Clear"));
        const auto startupFrame = camera.frameSlot().take();
        CHECK(startupFrame && startupFrame->valid && startupFrame->completeScan);
        sdk.emitImage(); CHECK(camera.frameSlot().take());
        camera.stop(); CHECK(sdk.stops == 1 && sdk.leases == 0);
    }
    // stop() must join a callback already inside production publish().
    sdk = {};
    {
        Mv3dLpCamera camera(QStringLiteral("TestCamera")); selectMode(camera);
        CHECK(camera.configureCapture(configuration()).ok() && camera.start().ok());
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        camera.frameSlot().setFramePublishedCallback([&] { entered.set_value(); released.wait(); });
        auto callback = std::async(std::launch::async, [] { sdk.emitImage(110); });
        entered.get_future().wait();
        auto stopping = std::async(std::launch::async, [&] { camera.stop(); });
        const bool waited = stopping.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
        release.set_value(); callback.get(); stopping.get();
        CHECK(waited && sdk.leases == 0 && sdk.stops == 1 && sdk.closes == 1);
    }
    std::cout << "Production Mv3dLp startup/cleanup with fake SDK passed\n";
    return 0;
}
