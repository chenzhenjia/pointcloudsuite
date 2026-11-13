#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_source.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/registration/handeye_transform.h>
#include "live_frame_policy.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "failed " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)
using namespace pcv::camera;

std::shared_ptr<CameraFrame> image(quint32 rows, quint32 number) {
    auto result = std::make_shared<CameraFrame>();
    result->valid = true; result->frameType = FrameType::PointCloudABC32f;
    result->rawFrameType = FrameType::DepthC16;
    result->boundarySource = ScanBoundarySource::SdkImageFrame;
    result->width = 8; result->height = rows; result->frameNumber = number;
    result->timeStamp = number * 100; result->receiveWorkNs = 100; result->sdkConversionNs = 50;
    result->data.resize(8 * rows * 12);
    for (quint32 i = 0; i < 8 * rows; ++i) {
        const float xyz[]{float(i % 8), float(i) + 1000, i == 3 ? std::numeric_limits<float>::quiet_NaN() : 20.f};
        std::memcpy(result->data.data() + i * 12, xyz, 12);
    }
    return result;
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir temp; CHECK(temp.isValid());
    QString error;
    CaptureConfiguration sdk{CaptureBoundaryMode::SdkImageFrame, 0, true};
    CaptureConfiguration unverified = sdk; unverified.sdkImageBoundaryVerified = false;
    CHECK(createCaptureSession(temp.path(), unverified, "mock", &error).isEmpty());
    CHECK(detail::preservesDeviceTrigger(sdk));
    CHECK(!detail::preservesDeviceTrigger({}));
    auto first = image(4, 700), second = image(3, 900), third = image(5, 1200);
    const auto sharedPayload = first->data;
    CHECK(detail::sdkImageBoundaryError(*first, sdk).isEmpty());
    CHECK(!detail::sdkImageBoundaryError(*first, unverified).isEmpty());
    // Matching raster shape and completeScan=true do not establish scan boundaries.
    auto profile = image(4, 701); profile->rawFrameType = FrameType::ProfileABC16;
    CHECK(!detail::sdkImageBoundaryError(*profile, sdk).isEmpty());
    CaptureFrameQueue profileQueue(sdk); CHECK(!profileQueue.push(profile));
    auto unknown = image(4, 702); unknown->boundarySource = ScanBoundarySource::Unspecified;
    CaptureFrameQueue unknownQueue(sdk); CHECK(!unknownQueue.push(unknown));
    auto shortFrame = image(1, 703);
    CHECK(!detail::sdkImageBoundaryError(*shortFrame, sdk).isEmpty());
    CaptureFrameQueue fixed(4); CHECK(!fixed.push(second));
    CaptureFrameQueue bound(sdk, 1); CHECK(bound.push(first)); CHECK(!bound.push(second));
    const QString firstError = bound.error(); bound.fail("later error"); CHECK(bound.error() == firstError);
    CHECK(bound.pop() == first && !bound.pop());
    CaptureFrameQueue byteBound(sdk, 2, 1); CHECK(!byteBound.push(first));
    auto queue = std::make_shared<CaptureFrameQueue>(sdk, 3);
    LatestFrameSlot slot; slot.setCaptureQueue(queue);
    slot.publish(first); slot.publish(second); slot.publish(third);
    CHECK(slot.droppedCount() == 2 && queue->received() == 3);
    const auto metrics = queue->statistics();
    CHECK(metrics.peakPending == 3 && metrics.lastHeight == 5 && metrics.sdkConversionNs == 150);
    slot.setCaptureQueue({}); queue->finish();
    CHECK(queue->pop() == first && queue->pop() == second && queue->pop() == third && !queue->pop());
    CHECK(queue->statistics().queueWaitNs > 0 && queue->statistics().pendingBytes == 0);
    // Shutdown concurrent with publication: accepted complete frames drain, no dangling callback target.
    auto concurrent = std::make_shared<CaptureFrameQueue>(sdk, 64);
    LatestFrameSlot shutdown; shutdown.setCaptureQueue(concurrent);
    std::thread producer([&shutdown, first] { for (int i = 0; i < 32; ++i) shutdown.publish(first); });
    concurrent->finish(); shutdown.setCaptureQueue({}); producer.join();
    while (concurrent->pop()) {}

    const auto root = createCaptureSession(temp.path(), sdk, "mock SDK image", &error);
    CHECK(!root.isEmpty() && error.isEmpty());
    const auto metadata = QJsonDocument::fromJson(bytes(QDir(root).filePath("session.json"))).object();
    CHECK(metadata.value("schema") == "pcv-camera-session-v2" && !metadata.contains("rows"));
    CaptureRecorder recorder(root, sdk);
    CHECK(recorder.queue()->push(first)); CHECK(recorder.queue()->push(second)); recorder.finish();
    CHECK(recorder.queue()->error().isEmpty() && recorder.statistics().saved == 2);
    CHECK(recorder.statistics().savedBytes > 0 && recorder.statistics().writeWorkNs > 0);
    const auto records = readCaptureSession(root, &error);
    CHECK(error.isEmpty() && records.size() == 2 && records.at(0).height == 4 && records.at(1).height == 3);
    const auto data = readCapturedFrame(records.at(1).directory);
    CHECK(data.ok && data.sourceIndices.size() == 23 && data.sourceIndices.at(3) == 4);
    // Disk failure does not report an uncommitted image as saved.
    const auto failedRoot = createCaptureSession(temp.path(), sdk, "mock", &error);
    CHECK(QDir().mkpath(QDir(failedRoot).filePath("frame_00000001/camera.ply")));
    CaptureRecorder failed(failedRoot, sdk); CHECK(failed.queue()->push(first)); failed.finish();
    CHECK(failed.savedFrames().isEmpty() && !failed.queue()->error().isEmpty());
    const auto offlineRoot = createCaptureSession(temp.path(), sdk, "mock", &error);
    CaptureRecorder offline(offlineRoot, sdk); CHECK(offline.queue()->push(third));
    offline.queue()->fail("offline"); offline.finish();
    CHECK(offline.savedFrames().size() == 1 && offline.queue()->error() == "offline");
    // Existing v1 reader still enforces fixed heights.
    const auto oldRoot = createCaptureSession(temp.path(), 4, "mock", &error);
    CaptureRecorder old(oldRoot, 4); CHECK(old.queue()->push(first)); old.finish();
    CHECK(readCaptureSession(oldRoot, &error).size() == 1 && error.isEmpty());

    const auto xml = QDir(root).filePath("input.xml"); QFile file(xml); CHECK(file.open(QIODevice::WriteOnly));
    file.write("<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>"); file.close();
    pcv::interface::StitchingRequest request; request.input.calibrationPath = xml;
    request.registration.options.enabled = false; request.registration.collectDiagnosticFrames = false;
    for (const auto &record : records) {
        pcv::interface::StitchingFrameInput input; input.captureFrameDirectory = record.directory;
        input.startPose = {0, 40, 0, 30, 0, 0}; input.endPose = {0, 0, 0, 30, 0, 0};
        request.input.frames.push_back(input);
    }
    const auto sharedInputs = request.input.frames;
    QVector<pointcloud::Point3D> baseline; QByteArray mapping;
    for (int workers : {0, 1, 2, 4}) {
        request.registration.parallelEnabled = workers != 0; request.registration.maxParallelTasks = workers;
        request.artifacts.outputDirectory = QDir(root).filePath(QStringLiteral("out%1").arg(workers));
        const auto result = pcv::interface::stitch(request);
        CHECK(result.success && result.points.size() == 54 && result.merge.registrationAttemptCount == 0);
        CHECK(result.merge.scanRatios.at(31) == 0.f && result.merge.scanRatios.last() == 1.f);
        CHECK(result.merge.sourceIndices.at(34) == 4);
        if (workers == 0) { baseline = result.points; mapping = bytes(result.manifest.mappingFile); }
        else CHECK(std::memcmp(baseline.constData(), result.points.constData(), baseline.size() * sizeof(pointcloud::Point3D)) == 0
            && mapping == bytes(result.manifest.mappingFile));
    }
    request.artifacts.outputDirectory = QDir(root).filePath("cancelled"); request.isCancelled = [] { return true; };
    CHECK(!pcv::interface::stitch(request).success);
    CHECK(!QFile::exists(QDir(request.artifacts.outputDirectory).filePath("stitched_robot_base.ply")));
    return 0;
}
