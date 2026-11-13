#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/camera/frame_source.h>
#include <pcv/interface/stitching_interface.h>
#include <pcv/registration/handeye_transform.h>
#include "scan_frame_assembler.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#define CHECK(test) do { if (!(test)) { std::cerr << "failed line " << __LINE__ << ": " << #test << '\n'; return 1; } } while (false)
using namespace pcv::camera;
std::shared_ptr<CameraFrame> frame(quint32 number, quint32 rows = 4) {
    auto f = std::make_shared<CameraFrame>();
    f->valid = true; f->frameType = FrameType::PointCloudABC32f;
    f->width = 8; f->height = rows; f->frameNumber = number; f->timeStamp = number * 100;
    f->data.resize(f->width * f->height * 12); f->intensityData.resize(f->width * f->height);
    for (quint32 i = 0; i < f->width * f->height; ++i) {
        const float xyz[3]{i == 3 ? std::numeric_limits<float>::quiet_NaN() : float(i % 8),
            float(9000 + i), float(20 + number)};
        std::memcpy(f->data.data() + i * 12, xyz, 12);
    }
    return f;
}
bool same(const QVector<pointcloud::Point3D> &a, const QVector<pointcloud::Point3D> &b) {
    return a.size() == b.size() && std::memcmp(a.constData(), b.constData(), a.size() * sizeof(pointcloud::Point3D)) == 0;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir temp; CHECK(temp.isValid());
    auto first = frame(1), second = frame(2);
    const auto shared = first->data; // Exercise Qt refcount > 1.
    const auto converted = convertPointCloudToPoints(*first);
    CHECK(converted.ok && converted.points.size() == 31 && converted.sourceIndices.at(3) == 4);
    CaptureFrameQueue bounded(4, 1); CHECK(bounded.push(first)); CHECK(!bounded.push(second));
    CHECK(!bounded.error().isEmpty() && bounded.pop() == first && !bounded.pop());
    CaptureFrameQueue byteBounded(4, 2, 1); CHECK(!byteBounded.push(first));
    auto queue = std::make_shared<CaptureFrameQueue>(4, 4);
    LatestFrameSlot slot; slot.setCaptureQueue(queue);
    CHECK(!slot.publish(first)); CHECK(slot.publish(second));
    CHECK(slot.droppedCount() == 1 && queue->received() == 2);
    slot.setCaptureQueue({}); queue->finish();
    CHECK(queue->pop() == first && queue->pop() == second && !queue->pop());
    CaptureFrameQueue bad(4); auto invalid = frame(3); invalid->completeScan = false;
    CHECK(!bad.push(invalid) && !bad.error().isEmpty());
    CaptureFrameQueue disconnect(4); auto event = std::make_shared<CameraFrame>(); event->kind = FrameKind::Exception;
    event->exceptionDescription = "offline"; CHECK(!disconnect.push(event)); CHECK(disconnect.error() == "offline");
    detail::ScanFrameAssembler assembler; assembler.configure(4, true);
    auto chunk1 = frame(100, 2), chunk2 = frame(102, 2);
    CHECK(assembler.append(chunk1).outcome == detail::ScanAssemblyOutcome::Pending);
    CHECK(assembler.append(chunk2).outcome == detail::ScanAssemblyOutcome::Complete);
    CHECK(assembler.append(frame(200, 5)).outcome == detail::ScanAssemblyOutcome::Rejected);
    CHECK(assembler.append(frame(300, 2)).outcome == detail::ScanAssemblyOutcome::Pending);
    CHECK(assembler.append(frame(400, 2)).outcome == detail::ScanAssemblyOutcome::Rejected);
    QString error;
    const auto root = createCaptureSession(temp.path(), 4, "mock", &error); CHECK(!root.isEmpty());
    CaptureRecorder recorder(root, 4); CHECK(recorder.queue()->push(first)); CHECK(recorder.queue()->push(second)); recorder.finish();
    CHECK(recorder.queue()->error().isEmpty()); CHECK(recorder.savedFrames().size() == 2);
    const auto records = readCaptureSession(root, &error); CHECK(error.isEmpty() && records.size() == 2);
    const auto data = readCapturedFrame(records.at(0).directory); CHECK(data.ok);
    CHECK(same(data.points, converted.points) && data.sourceIndices == converted.sourceIndices);
    CHECK(!readCapturedFrame(records.at(0).directory, [] { return true; }).ok);
    CHECK(writeCaptureParameters(root, {{"test", true}}, &error)); CHECK(readCaptureParameters(root).value("test").toBool());
    pointcloud::HandEyeCalibration calibration; calibration.valid = true;
    pointcloud::RobotPose start{0, 100, 0, 30, 0, 0}, end{0, 40, 0, 30, 0, 0};
    pointcloud::CloudTransformOptions options;
    options.useRasterRowProgress = true; options.rasterWidth = 8; options.rasterHeight = 4;
    options.rasterSourceIndices = data.sourceIndices;
    const auto identity = pointcloud::transformLineScanToRobotBase(data.points, calibration, start, end, options);
    CHECK(identity.ok && identity.points.size() == 31);
    CHECK(identity.sourceIndices == data.sourceIndices);
    for (int i = 0; i < identity.points.size(); ++i) {
        const float ratio = float(data.sourceIndices.at(i) / 8) / 3;
        CHECK(std::abs(identity.scanRatios.at(i) - ratio) < 1.e-6f);
        const auto expected = pointcloud::transformPointToRobotBase(
            pointcloud::lineProfileCameraPoint(data.points.at(i)),
            pointcloud::interpolateRobotTransform(pointcloud::robotPoseToMatrix(start), pointcloud::robotPoseToMatrix(end), ratio), calibration);
        CHECK(std::abs(expected.y - identity.points.at(i).y) < 1.e-5f);
    }
    const auto pointCopy = data.points; const auto indexCopy = options.rasterSourceIndices;
    for (int workers : {1, 2, 4}) {
        options.parallelEnabled = true; options.maxParallelTasks = workers;
        CHECK(same(identity.points, pointcloud::transformLineScanToRobotBase(data.points, calibration, start, end, options).points));
    }
    options.rasterSourceIndices[0] = -1; CHECK(!pointcloud::transformLineScanToRobotBase(data.points, calibration, start, end, options).ok);
    const auto xml = QDir(root).filePath("handeye.xml"); QFile xmlFile(xml); CHECK(xmlFile.open(QIODevice::WriteOnly));
    xmlFile.write("<ArithConfig><RTmatDepth2robot><RotMat r00=\"1\" r01=\"0\" r02=\"0\" r10=\"0\" r11=\"1\" r12=\"0\" r20=\"0\" r21=\"0\" r22=\"1\"/><TVec t0=\"0\" t1=\"0\" t2=\"0\"/></RTmatDepth2robot></ArithConfig>"); xmlFile.close();
    pcv::interface::StitchingRequest request; request.input.calibrationPath = xml;
    for (const auto &record : records) {
        pcv::interface::StitchingFrameInput input; input.captureFrameDirectory = record.directory;
        input.startPose = start; input.endPose = end; request.input.frames.push_back(input);
    }
    request.registration.options.enabled = false; request.registration.collectDiagnosticFrames = false;
    QVector<pointcloud::Point3D> baseline;
    QByteArray baselineMap;
    for (int workers : {0, 1, 2, 4}) {
        request.registration.parallelEnabled = workers != 0; request.registration.maxParallelTasks = workers;
        request.artifacts.outputDirectory = QDir(root).filePath(QStringLiteral("output%1").arg(workers));
        const auto result = pcv::interface::stitch(request);
        if (!result.success) std::cerr << result.message.toStdString() << '\n';
        CHECK(result.success && result.points.size() == 62 && result.merge.registrationAttemptCount == 0);
        CHECK(result.merge.sourceIndices.at(3) == 4 && result.merge.scanRatios.at(8) > 0);
        CHECK(result.diagnostics.contains("not_run"));
        QFile map(result.manifest.mappingFile); CHECK(map.open(QIODevice::ReadOnly));
        const auto bytes = map.readAll();
        if (workers == 0) { baseline = result.points; baselineMap = bytes; }
        else CHECK(same(baseline, result.points) && baselineMap == bytes);
    }
    request.artifacts.outputDirectory = QDir(root).filePath("cancelled");
    request.isCancelled = [] { return true; }; CHECK(!pcv::interface::stitch(request).success);
    CHECK(!QFile::exists(QDir(request.artifacts.outputDirectory).filePath("stitched_robot_base.ply")));
    request.isCancelled = {}; request.registration.options.enabled = true;
    CHECK(!pcv::interface::stitch(request).success);
    // An occupied output target forces the transaction to fail, without touching other frames.
    const auto brokenRoot = createCaptureSession(temp.path(), 4, "mock", &error);
    CHECK(!brokenRoot.isEmpty()); QDir().mkpath(QDir(brokenRoot).filePath("frame_00000001/camera.ply"));
    CaptureRecorder diskFailure(brokenRoot, 4); CHECK(diskFailure.queue()->push(first)); diskFailure.finish();
    CHECK(!diskFailure.queue()->error().isEmpty() && diskFailure.savedFrames().isEmpty());
    auto uncommitted = QDir(root).filePath("frame_00000003"); QDir().mkpath(uncommitted);
    CHECK(readCaptureSession(root, &error).size() == 2);
    QFile corrupt(QDir(records.at(0).directory).filePath("raster_indices.bin"));
    CHECK(corrupt.open(QIODevice::WriteOnly)); corrupt.write("bad"); corrupt.close();
    CHECK(!readCapturedFrame(records.at(0).directory).ok);
    return 0;
}
