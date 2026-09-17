#pragma once

#include <pcv/camera/frame_data.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <mutex>

namespace pcv::camera {

// A frame source publishes the latest point-cloud frame to a single consumer.
// The producer side runs on the SDK callback thread; consumers are notified
// and take the latest frame from their event handler. The slot keeps only the newest frame and
// drops older ones (latest-frame-wins), bounding latency under load.
class LatestFrameSlot {
public:
    LatestFrameSlot() = default;
    ~LatestFrameSlot() = default;

    LatestFrameSlot(const LatestFrameSlot &) = delete;
    LatestFrameSlot &operator=(const LatestFrameSlot &) = delete;

    // Installs/removes the producer-thread notification callback. Both
    // operations are synchronized with publish(), so teardown can clear the
    // callback before destroying its consumer.
    void setFramePublishedCallback(std::function<void()> callback);
    void clearFramePublishedCallback();

    // Called from the producer thread. Replaces the current frame with the
    // newest one. Returns true when a previously published frame was dropped.
    bool publish(std::shared_ptr<const CameraFrame> frame);

    // Called from the consumer thread. Atomically takes the latest frame and
    // clears the slot. Returns nullptr when no frame is available.
    std::shared_ptr<const CameraFrame> take();

    // Non-destructive peek of the current frame number.
    std::uint32_t latestFrameNumber() const;

    // Acquisition diagnostics. Counters are monotonic for the lifetime of
    // the slot and are safe to query from the consumer thread.
    quint64 publishedCount() const;
    quint64 droppedCount() const;
    void resetStatistics();

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const CameraFrame> m_latest;
    std::function<void()> m_onFramePublished;
    quint64 m_publishedCount = 0;
    quint64 m_droppedCount = 0;
};

// Abstract device-level acquisition interface shared by the real SDK wrapper
// and the mock source, so the pipeline does not depend on a live camera.
class FrameSource {
public:
    virtual ~FrameSource() = default;

    // Opens the device and starts continuous measurement. After this call the
    // frame slot receives point-cloud frames via the SDK callback.
    virtual CameraError start() = 0;

    // Stops measurement and releases the device.
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;

    virtual QString deviceSummary() const = 0;

    // The slot the SDK callback publishes into.
    virtual LatestFrameSlot &frameSlot() = 0;

    // Converts one owned frame into project points. The live SDK source uses
    // the vendor conversion (MapDepthToPointCloud / ImageConvert) when the
    // frame type requires it; the mock source uses the pure-function fallback.
    virtual PointCloudConvertResult convertFrame(
        const CameraFrame &frame,
        const PointCloudConvertOptions &options = {}) = 0;

    virtual bool supportsSoftTrigger() const { return false; }
    virtual CameraError softTrigger() { return {QStringLiteral("soft trigger is not supported")}; }
    virtual bool supportsReconnect() const { return false; }
    virtual CameraError reconnectOnce()
    {
        return {QStringLiteral("reconnect is not supported")};
    }
};

} // namespace pcv::camera
