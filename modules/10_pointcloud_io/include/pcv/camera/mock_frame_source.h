#pragma once

#include <pcv/camera/frame_source.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace pcv::camera {

// Synthetic frame source for tests and for running the pipeline without a
// live camera. Generates a flat synthetic point cloud once per start() and
// republishes it on demand. Mimics the SDK callback contract: publish() is
// the only way frames enter the slot.
class MockFrameSource final : public FrameSource {
public:
    explicit MockFrameSource(std::uint32_t width = 640,
                             std::uint32_t height = 480);

    CameraError start() override;
    void stop() override;
    bool isRunning() const override;
    QString deviceSummary() const override;
    LatestFrameSlot &frameSlot() override;

    // Emits one synthetic frame into the slot (called by tests or a timer).
    void emitFrame();

    // Publishes a FrameKind::Exception event (simulates device offline).
    void emitException(const QString &description = {});

    PointCloudConvertResult convertFrame(
        const CameraFrame &frame,
        const PointCloudConvertOptions &options = {}) override;
    bool supportsSoftTrigger() const override { return false; }
    bool supportsReconnect() const override { return false; }

private:
    std::shared_ptr<const CameraFrame> makeSyntheticFrame(std::uint32_t frameNumber) const;

    std::uint32_t m_width;
    std::uint32_t m_height;
    std::atomic<bool> m_running{false};
    std::uint32_t m_frameCounter = 0;
    LatestFrameSlot m_slot;
};

} // namespace pcv::camera
