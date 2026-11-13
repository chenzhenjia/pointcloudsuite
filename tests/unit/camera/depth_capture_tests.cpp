#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/interface/stitching_interface.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
#include <cstring>

#define CHECK(c) do { if (!(c)) { std::cerr << "line " << __LINE__ << ": " #c "\n"; return 1; } } while (false)
using namespace pcv::camera;
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir temp; CHECK(temp.isValid());
    CHECK(QDir(temp.path()).mkpath(QString::fromUtf8("深度会话")));
    const auto parent = QDir(temp.path()).filePath(QString::fromUtf8("深度会话"));
    auto frame = std::make_shared<CameraFrame>();
    frame->width = 32; frame->height = 16; frame->valid = true;
    frame->frameType = frame->rawFrameType = FrameType::DepthC16;
    frame->boundarySource = ScanBoundarySource::SdkImageFrame;
    frame->frameNumber = 19; frame->timeStamp = 1100; frame->sdkReserved = QByteArray(8,0);
    frame->xScale = .2f; frame->yScale = .3f; frame->zScale = .01f;
    frame->xOffset = -50; frame->yOffset = 13; frame->zOffset = -9;
    frame->data.resize(32*16*2); frame->intensityData = QByteArray(32*16,char(130));
    frame->intensityWidth = 32; frame->intensityHeight = 16;
    for (int i = 0; i < 32*16; ++i) qToLittleEndian<qint16>(i%17 == 0 ? -32768 : qint16(100+i),frame->data.data()+i*2);
    for (int i = 0; i < 16; ++i) { frame->exposureTimeStamps.append(100+i*7); frame->encoders.append(-17+i); }
    const auto environment = cameraConversionEnvironment();
    frame->sdkVersion = environment.error.isEmpty() ? environment.sdkVersion : "unavailable-sdk";
    frame->conversionFingerprint = environment.error.isEmpty() ? environment.fingerprint : QString(64,'a');
    const auto sharedPayload = frame->data;
    CaptureConfiguration config{CaptureBoundaryMode::SdkImageFrame,0,true};
    config.storeRawDepth = true; config.requireIntensity = true;
    QString error; const auto session = createCaptureSession(parent,config,"depth-test",&error); CHECK(!session.isEmpty());
    CaptureRecorder recorder(session,config); CHECK(recorder.queue()->push(frame)); recorder.finish();
    CHECK(recorder.queue()->error().isEmpty() && recorder.savedFrames().size() == 1);
    CHECK(recorder.latestPreview().isEmpty() && recorder.latestDepthFrame() == frame);
    const auto records = readCaptureSession(session,&error); CHECK(error.isEmpty() && records.size() == 1 && records.at(0).depthAvailable);
    const auto dir = records.at(0).directory;
    CHECK(!QFile::exists(QDir(dir).filePath("camera.ply")) && !QFile::exists(QDir(dir).filePath("raster_indices.bin")));
    const auto preview = readCapturedFrame(dir,{},CaptureFrameReadMode::PreviewOnly);
    CHECK(preview.ok && preview.points.isEmpty() && preview.sourceIndices.isEmpty() && preview.rawFrame);
    CHECK(preview.rawFrame->data == sharedPayload && preview.intensityMono8 == frame->intensityData);
    CHECK(preview.rawFrame->exposureTimeStamps == frame->exposureTimeStamps && preview.rawFrame->encoders == frame->encoders);
    CHECK(preview.rawFrame->xScale == frame->xScale && preview.rawFrame->zOffset == frame->zOffset);
    CHECK(!readCapturedFrame(dir,[]{return true;},CaptureFrameReadMode::GeometryPreview).ok);
    if (environment.error.isEmpty()) {
        const auto direct = materializeCameraFrame(*frame);
        const auto loaded = readCapturedFrame(dir,{},CaptureFrameReadMode::GeometryOnly);
        if (!direct.ok || !loaded.ok) std::cerr << direct.error.toStdString() << " | " << loaded.error.toStdString() << '\n';
        CHECK(direct.ok && loaded.ok && direct.points.size() == loaded.points.size());
        CHECK(direct.sourceIndices == loaded.sourceIndices && direct.totalSamples == 512);
        CHECK(std::memcmp(direct.points.constData(),loaded.points.constData(),size_t(direct.points.size())*sizeof(pointcloud::Point3D)) == 0);
        auto incompatible = *frame; incompatible.conversionFingerprint = QString(64,'0'); CHECK(!materializeCameraFrame(incompatible).ok);
    } else CHECK(!readCapturedFrame(dir,{},CaptureFrameReadMode::GeometryOnly).ok);
    CHECK(QFile::rename(QDir(dir).filePath("intensity.mono8"),QDir(dir).filePath("intensity-hidden.mono8")));
    CHECK(readCapturedFrame(dir,{},CaptureFrameReadMode::GeometryPreview).ok);
    CHECK(!readCapturedFrame(dir,{},CaptureFrameReadMode::PreviewOnly).ok);
    QFile depth(QDir(dir).filePath("depth.c16")); CHECK(depth.open(QIODevice::ReadWrite)); CHECK(depth.write("XX",2) == 2); depth.close();
    CHECK(!readCapturedFrame(dir,{},CaptureFrameReadMode::GeometryPreview).ok);
    CaptureFrameQueue queue(config,1); CHECK(queue.push(frame)); CHECK(!queue.push(frame));
    const auto firstError = queue.error(); queue.fail("later"); CHECK(queue.error() == firstError);
    CHECK(queue.pop() == frame && !queue.pop() && queue.statistics().pendingBytes == 0);
    CHECK(frame->data == sharedPayload);
    std::cout << "depth storage, provenance, cancellation, no-materialization preview and SDK roundtrip passed\n";
    return 0;
}
