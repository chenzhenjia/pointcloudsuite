#pragma once

#include <pcv/camera/frame_converter.h>
#include <pcv/camera/frame_source.h>

#include <QString>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>

namespace pcv::camera {

// RAII wrapper around the Hikvision Mv3dLp SDK. Owns the device handle and
// the SDK image-data callback. The callback thread publishes owned frame
// copies into the internal LatestFrameSlot; the GUI receives a queued
// notification and takes the latest frame.
//
// Range_Image is the default for DP-series devices. Callback data is
// normalized to owned ABC32f complete scans before consumers receive it.
// The SDK types are confined to the .cpp translation unit (pimpl) so this
// public header has no dependency on the vendor SDK headers.
class Mv3dLpCamera final : public FrameSource {
public:
    // Opens the device by serial number at construction time (no measurement).
    explicit Mv3dLpCamera(const QString &serialNumber,
                          std::uint32_t ipTimeoutMs = 3000);
    ~Mv3dLpCamera() override;

    Mv3dLpCamera(const Mv3dLpCamera &) = delete;
    Mv3dLpCamera &operator=(const Mv3dLpCamera &) = delete;

    // Enumerates cameras present in the environment. Returns an empty list
    // and fills error when the SDK is unavailable or no device is found.
    static QVector<DeviceInfo> enumerateDevices(CameraError *error = nullptr);

    // FrameSource interface.
    CameraError start() override;
    void stop() override;
    bool isRunning() const override;
    QString deviceSummary() const override;
    LatestFrameSlot &frameSlot() override;
    PointCloudConvertResult convertFrame(
        const CameraFrame &frame,
        const PointCloudConvertOptions &options = {}) override;
    bool supportsSoftTrigger() const override;
    CameraError softTrigger() override;
    bool supportsReconnect() const override { return true; }
    CameraError reconnectOnce() override;

    // Optional explicit image mode. Defaults to RangeImage; PointCloudImage
    // remains an explicit profile-batch diagnostic path.
    void requestImageMode(ImageMode mode);
    // Selects the device trigger mode before the next start(). Continuous
    // acquisition uses the device default when its optional TriggerMode node
    // is unavailable; software trigger requires verified TriggerMode and
    // TriggerSource support.
    void requestSoftwareTrigger(bool enabled);
    std::optional<ImageMode> activeImageMode() const;

    // Triggers one frame in software-trigger mode (MV3D_LP_SoftTrigger).
    // One reconnect attempt: stop, close, reopen by serial, re-register
    // callbacks and restart measurement. Returns OK when streaming resumed.
    // Called from the UI thread after an exception event was observed.
    // True when the device reports a virtual/emulated source (3DMVS virtual
    // camera), which supports the same acquisition flow for offline testing.
    bool isVirtualDevice() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace pcv::camera
