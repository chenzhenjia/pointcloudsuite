#include <pcv/camera/frame_source.h>
#include <pcv/camera/mock_frame_source.h>

#include <QCoreApplication>

#include <iostream>
#include <memory>

namespace {
bool require(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Mock source starts and publishes a valid point-cloud frame.
    {
        pcv::camera::MockFrameSource source(64, 48);
        if (!require(source.frameSlot().take() == nullptr,
                     "no frame should be available before start")) return 1;
        const auto error = source.start();
        if (!require(error.ok() && source.isRunning(),
                     "mock source should start cleanly")) return 1;
        auto frame = source.frameSlot().take();
        if (!require(frame != nullptr, "start() should publish one frame")) return 1;
        if (!require(frame->frameType == pcv::camera::FrameType::PointCloudABC32f
                         && frame->width == 64 && frame->height == 48
                         && frame->dataPointCount() == 64 * 48,
                     "mock frame should be a 64x48 point cloud")) return 1;
        source.stop();
        if (!require(!source.isRunning(), "mock source should stop")) return 1;
    }

    // Latest-frame-wins: publishing several frames keeps only the newest.
    {
        pcv::camera::MockFrameSource source(8, 8);
        source.start();
        source.emitFrame(); // frame 2
        source.emitFrame(); // frame 3
        auto frame = source.frameSlot().take();
        if (!require(frame != nullptr && frame->frameNumber == 3,
                     "frame slot must keep only the latest frame")) return 1;
        if (!require(source.frameSlot().take() == nullptr,
                     "take() must clear the slot")) return 1;
        source.stop();
    }

    // Frame source interface works through the abstract base.
    {
        pcv::camera::MockFrameSource source(16, 16);
        pcv::camera::FrameSource &abstract = source;
        if (!require(!abstract.deviceSummary().isEmpty(),
                     "deviceSummary should describe the source")) return 1;
        abstract.start();
        if (!require(abstract.isRunning(), "abstract start should run")) return 1;
        auto frame = abstract.frameSlot().take();
        if (!require(frame != nullptr, "abstract frameSlot should deliver")) return 1;
        abstract.stop();
    }

    // onFramePublished notification fires on publish (event-driven path).
    {
        pcv::camera::MockFrameSource source(4, 4);
        int notifications = 0;
        source.frameSlot().setFramePublishedCallback([&notifications]() { ++notifications; });
        source.emitFrame();
        source.emitFrame();
        if (!require(notifications == 2,
                     "onFramePublished should fire once per publish")) return 1;
        source.frameSlot().take();
        if (!require(notifications == 2,
                     "take() must not trigger onFramePublished")) return 1;
        source.frameSlot().clearFramePublishedCallback();
        source.emitFrame();
        if (!require(notifications == 2,
                     "clearing the callback must disable notifications")) return 1;
    }

    {
        pcv::camera::MockFrameSource source;
        pcv::camera::FrameSource &abstract = source;
        if (!require(!abstract.supportsSoftTrigger() && !abstract.supportsReconnect(),
                     "mock source must not advertise SDK-only capabilities")) return 1;
        if (!require(!abstract.softTrigger().ok() && !abstract.reconnectOnce().ok(),
                     "unsupported mock operations must return errors")) return 1;
    }

    // Exception events carry FrameKind::Exception and reach the slot.
    {
        pcv::camera::MockFrameSource source(4, 4);
        source.emitException(QStringLiteral("device is offline"));
        auto event = source.frameSlot().take();
        if (!require(event != nullptr
                         && event->kind == pcv::camera::FrameKind::Exception
                         && !event->valid
                         && event->exceptionDescription == QStringLiteral("device is offline"),
                     "exception event should carry kind/description")) return 1;
        if (!require(source.convertFrame(*event).error.contains(QStringLiteral("unsupported")),
                     "converting an exception event must fail with a clear error")) return 1;
    }

    // convertFrame dispatch: depth and profile frames through the mock.
    {
        pcv::camera::CameraFrame depth;
        depth.frameType = pcv::camera::FrameType::DepthC16;
        depth.width = 2;
        depth.height = 1;
        depth.valid = true;
        depth.zScale = 1.0f;
        QByteArray payload;
        payload.resize(2 * 2);
        auto *raw = reinterpret_cast<qint16 *>(payload.data());
        raw[0] = 10;
        raw[1] = 20;
        depth.data = payload;

        pcv::camera::MockFrameSource source(2, 1);
        const auto result = source.convertFrame(depth);
        if (!require(result.ok && result.points.size() == 2,
                     "mock convertFrame should convert a depth frame")) return 1;
        if (!require(result.points[1].z == 20.0f,
                     "depth z should use zScale")) return 1;
    }

    std::cout << "frame_source_tests: PASS\n";
    return 0;
}
