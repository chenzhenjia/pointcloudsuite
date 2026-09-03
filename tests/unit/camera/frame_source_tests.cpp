#include <pcv/camera/frame_source.h>
#include <pcv/camera/mock_frame_source.h>

#include "live_frame_policy.h"
#include "scan_frame_assembler.h"
#include "trigger_mode_policy.h"

#include <QCoreApplication>

#include <iostream>
#include <memory>

namespace {
std::shared_ptr<pcv::camera::CameraFrame> makeScanChunk(
    std::uint32_t width, std::uint32_t height, std::uint32_t firstRow)
{
    auto frame = std::make_shared<pcv::camera::CameraFrame>();
    frame->frameType = pcv::camera::FrameType::PointCloudABC32f;
    frame->width = width;
    frame->height = height;
    frame->frameNumber = firstRow;
    frame->valid = true;
    frame->data = QByteArray(qsizetype(width) * qsizetype(height) * 12, '\0');
    return frame;
}
} // namespace

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

    // 3DMVS HFR devices may expose the current transfer batch in nCurValue
    // (for example 24) while nMax is the complete scan capacity (for example
    // 3000). The assembler must use nMax when it is available.
    {
        const auto selection = pcv::camera::detail::selectExpectedScanHeight(24, 3000);
        if (!require(selection.rows == 3000
                         && selection.source == pcv::camera::detail::ExpectedScanHeightSource::Maximum,
                     "Height selection should prefer nMax over nCurValue")) return 1;
        const auto fallback = pcv::camera::detail::selectExpectedScanHeight(24, 0);
        if (!require(fallback.rows == 24
                         && fallback.source == pcv::camera::detail::ExpectedScanHeightSource::Current,
                     "Height selection should fall back to nCurValue when nMax is unavailable")) return 1;
    }

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

    // DP-series HFR callbacks may split one configured Height into multiple
    // profile batches. nFrameNum is a running sequence and need not begin at
    // zero when the application starts. Only a complete, contiguous scan is
    // publishable.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(4);
        auto first = assembler.append(makeScanChunk(2, 3, 100));
        if (!require(first.outcome == pcv::camera::detail::ScanAssemblyOutcome::Pending
                         && assembler.accumulatedRows() == 3
                         && assembler.fragmentCount() == 1,
                     "first partial scan batch should remain pending")) return 1;
        auto complete = assembler.append(makeScanChunk(2, 1, 103));
        if (!require(complete.outcome == pcv::camera::detail::ScanAssemblyOutcome::Complete
                         && complete.frame != nullptr
                         && complete.frame->width == 2 && complete.frame->height == 4
                         && complete.frame->data.size() == 2 * 4 * 12
                         && complete.frame->scanFragmentCount == 2
                         && complete.frame->completeScan,
                     "contiguous final batch should publish one complete scan")) return 1;
    }

    // A callback with the configured full scan height is accepted directly.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(3000);
        auto complete = assembler.append(makeScanChunk(2, 3000, 17));
        if (!require(complete.outcome == pcv::camera::detail::ScanAssemblyOutcome::Complete
                         && complete.frame != nullptr
                         && complete.frame->height == 3000
                         && complete.frame->completeScan,
                     "full-height callback should publish one complete scan")) return 1;
    }

    // A too-tall callback is rejected with both actual and configured heights.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(24);
        const auto rejected = assembler.append(makeScanChunk(2, 3000, 0));
        if (!require(rejected.outcome == pcv::camera::detail::ScanAssemblyOutcome::Rejected
                         && rejected.error.contains(QStringLiteral("3000"))
                         && rejected.error.contains(QStringLiteral("24")),
                     "height mismatch should report actual and configured rows")) return 1;
    }

    // A single Range_Image callback matching Height is already a complete scan.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(3);
        auto complete = assembler.append(makeScanChunk(2, 3, 0));
        if (!require(complete.outcome == pcv::camera::detail::ScanAssemblyOutcome::Complete
                         && complete.frame->scanFragmentCount == 1
                         && complete.frame->accumulatedScanRows == 3,
                     "full Range_Image raster should bypass partial assembly")) return 1;
    }

    // Gaps, duplicates, reverse rows, and width changes must clear the
    // partial scan. A later sequence value can then anchor a new acquisition.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(4);
        assembler.append(makeScanChunk(2, 1, 0));
        auto gap = assembler.append(makeScanChunk(2, 1, 2));
        if (!require(gap.outcome == pcv::camera::detail::ScanAssemblyOutcome::Rejected
                         && assembler.accumulatedRows() == 0,
                     "missing profile rows must reject the partial scan")) return 1;
        assembler.append(makeScanChunk(2, 1, 0));
        assembler.append(makeScanChunk(2, 1, 1));
        auto duplicate = assembler.append(makeScanChunk(2, 1, 1));
        if (!require(duplicate.outcome == pcv::camera::detail::ScanAssemblyOutcome::Rejected,
                     "duplicate profile rows must reject the partial scan")) return 1;
        assembler.append(makeScanChunk(2, 1, 0));
        auto widthChange = assembler.append(makeScanChunk(3, 1, 1));
        if (!require(widthChange.outcome == pcv::camera::detail::ScanAssemblyOutcome::Rejected,
                     "width changes must reject the partial scan")) return 1;
        auto next = assembler.append(makeScanChunk(2, 4, 100));
        if (!require(next.outcome == pcv::camera::detail::ScanAssemblyOutcome::Complete
                         && next.frame->discardedScanCount >= 3,
                     "a later full scan must recover after rejected batches")) return 1;
    }

    // The assembler rejects payloads that would exceed its bounded buffer.
    {
        pcv::camera::detail::ScanFrameAssembler assembler(4, 24);
        auto oversized = assembler.append(makeScanChunk(2, 4, 0));
        if (!require(oversized.outcome == pcv::camera::detail::ScanAssemblyOutcome::Rejected,
                     "assembly memory limit must reject oversized complete scans")) return 1;
    }

    // Latest-frame-wins: publishing several frames keeps only the newest.
    {
        pcv::camera::MockFrameSource source(8, 8);
        source.start();
        source.emitFrame(); // frame 2
        source.emitFrame(); // frame 3
        if (!require(source.frameSlot().publishedCount() == 3
                         && source.frameSlot().droppedCount() == 2,
                     "frame slot statistics should count published and dropped frames")) return 1;
        auto frame = source.frameSlot().take();
        if (!require(frame != nullptr && frame->frameNumber == 3,
                     "frame slot must keep only the latest frame")) return 1;
        if (!require(source.frameSlot().take() == nullptr,
                     "take() must clear the slot")) return 1;
        source.frameSlot().resetStatistics();
        if (!require(source.frameSlot().publishedCount() == 0
                         && source.frameSlot().droppedCount() == 0,
                     "frame slot statistics should reset")) return 1;
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

    // PointCloud_Image is fail-closed: a real SDK C16/Profile callback does
    // not match the requested ABC32f path and must never use approximation.
    {
        using pcv::camera::FrameType;
        using pcv::camera::ImageMode;
        using pcv::camera::detail::CallbackFrameAction;
        using pcv::camera::detail::callbackFrameAction;
        using pcv::camera::detail::imageModeFrameTypeError;
        if (!require(callbackFrameAction(FrameType::PointCloudABC32f)
                         == CallbackFrameAction::CopyPointCloud,
                     "ABC32f callback frames must be copied directly")) return 1;
        if (!require(callbackFrameAction(FrameType::ProfileABC16)
                         == CallbackFrameAction::ConvertProfileToPointCloud,
                     "ABC16 callback frames must use raw SDK image conversion")) return 1;
        if (!require(callbackFrameAction(FrameType::DepthC16)
                         == CallbackFrameAction::ConvertDepthToPointCloud,
                     "C16 callback frames must use raw SDK depth conversion")) return 1;
        if (!require(callbackFrameAction(FrameType::Mono8) == CallbackFrameAction::Reject,
                     "unsupported callback frame types must be rejected")) return 1;
        if (!require(imageModeFrameTypeError(ImageMode::PointCloudImage,
                                             ImageMode::PointCloudImage,
                                             FrameType::PointCloudABC32f).isEmpty(),
                     "PointCloudImage must accept ABC32f")) return 1;
        const QString depthError = imageModeFrameTypeError(ImageMode::PointCloudImage,
                                                            ImageMode::PointCloudImage,
                                                            FrameType::DepthC16);
        if (!require(depthError.contains(QStringLiteral("does not match requested PointCloudImage")),
                     "PointCloudImage must reject C16 with an explicit mode error")) return 1;
        if (!require(!imageModeFrameTypeError(ImageMode::PointCloudImage,
                                              ImageMode::PointCloudImage,
                                              FrameType::ProfileABC16).isEmpty(),
                     "PointCloudImage must reject profile output")) return 1;
    }

    // DP2240 may not expose optional trigger nodes. Continuous acquisition
    // can use the SDK default, while software-trigger mode must reject it.
    {
        using pcv::camera::detail::continuousTriggerProbeMayContinue;
        using pcv::camera::detail::softwareTriggerProbeMustReject;
        if (!require(continuousTriggerProbeMayContinue(0x80060004u),
                     "continuous mode must tolerate unsupported TriggerMode")) return 1;
        if (!require(continuousTriggerProbeMayContinue(0x80060001u),
                     "continuous mode must tolerate unsupported trigger node")) return 1;
        if (!require(softwareTriggerProbeMustReject(0x80060004u),
                     "software trigger must reject missing TriggerMode")) return 1;
        if (!require(!continuousTriggerProbeMayContinue(0x80060003u),
                     "call-order errors must not be ignored")) return 1;
    }

    std::cout << "frame_source_tests: PASS\n";
    return 0;
}
