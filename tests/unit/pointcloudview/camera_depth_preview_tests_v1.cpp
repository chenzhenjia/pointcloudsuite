#include "features/camera_depth_preview_v1.h"
#include "process_memory_usage_v1.h"
#include <pcv/camera/capture_session_v1.h>
#include <pcv/render/depth_image_canvas_v1.h>
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <iostream>
#include <cstring>

#define CHECK(c) do { if (!(c)) { std::cerr << "line " << __LINE__ << ": " #c "\n"; return 1; } } while (false)
namespace {
bool waitUntil(const std::function<bool()> &done, qint64 limit = 30000) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed()<limit) { QApplication::processEvents(); QThread::msleep(1); }
    QApplication::processEvents(); return done();
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
std::shared_ptr<pcv::camera::CameraFrame> frame(int size) {
    auto f = std::make_shared<pcv::camera::CameraFrame>();
    f->valid = true; f->frameType = f->rawFrameType = pcv::camera::FrameType::DepthC16;
    f->width = f->height = size; f->frameNumber = 7; f->data.resize(size*size*2);
    f->xScale = f->yScale = .2f; f->zScale = .01f; f->sdkReserved = QByteArray(8,0);
    f->intensityData = QByteArray(size*size,char(255)); f->intensityWidth = f->intensityHeight = size;
    for (int i=0;i<size*size;++i) qToLittleEndian<qint16>(qint16(1000),f->data.data()+i*2);
    const auto env = pcv::camera::cameraConversionEnvironment(); f->sdkVersion = env.sdkVersion; f->conversionFingerprint = env.fingerprint;
    return f;
}
}
struct CameraDepthPreviewTestAccess {
    static int run(const QStringList &args) {
        auto f = frame(64);
        const bool benchmark = args.contains("--fixture");
        QString fixture;
        if (benchmark) {
            fixture = args.value(args.indexOf("--fixture")+1); const QDir dir(fixture);
            const auto meta = QJsonDocument::fromJson(bytes(dir.filePath("virtual-frame.json"))).object();
            CHECK(!meta.isEmpty());
            f->width = quint32(meta.value("width").toInteger()); f->height = quint32(meta.value("height").toInteger());
            f->frameNumber = quint32(meta.value("number").toInteger()); f->timeStamp = meta.value("timestamp").toInteger();
            f->xScale = float(meta.value("xs").toDouble()); f->yScale = float(meta.value("ys").toDouble()); f->zScale = float(meta.value("zs").toDouble());
            f->xOffset = meta.value("xo").toInt(); f->yOffset = meta.value("yo").toInt(); f->zOffset = meta.value("zo").toInt();
            f->sdkReserved = QByteArray::fromHex(meta.value("reserved").toString().toLatin1());
            f->data = bytes(dir.filePath("virtual-depth.c16")); CHECK(!f->data.isEmpty());
            f->intensityData = bytes(dir.filePath("virtual-intensity.bin")); CHECK(!f->intensityData.isEmpty());
            f->intensityWidth = f->width; f->intensityHeight = f->height;
            for (const auto &field : {QStringLiteral("exposure"),QStringLiteral("encoder")}) {
                const auto buffer = bytes(dir.filePath("virtual-"+field+".bin"));
                for (int i=0;i<buffer.size()/(field=="exposure" ? 8 : 4);++i) {
                    if (field=="exposure") f->exposureTimeStamps.append(qFromLittleEndian<qint64>(buffer.constData()+i*8));
                    else f->encoders.append(qFromLittleEndian<qint32>(buffer.constData()+i*4));
                }
            }
            const auto baselineBytes = bytes(dir.filePath("virtual-baseline.abc32f"));
            pcv::camera::CameraFrame baseline; baseline.frameType = pcv::camera::FrameType::PointCloudABC32f;
            baseline.width = f->width; baseline.height = f->height; baseline.valid = true; baseline.data = baselineBytes;
            const auto original = pcv::camera::convertPointCloudToPoints(baseline);
            const auto copied = pcv::camera::materializeCameraFrame(*f);
            CHECK(original.ok && copied.ok && original.sourceIndices == copied.sourceIndices && original.points.size() == copied.points.size());
            CHECK(std::memcmp(original.points.constData(),copied.points.constData(),size_t(original.points.size())*sizeof(pointcloud::Point3D)) == 0);
            QTemporaryDir roundtrip; CHECK(roundtrip.isValid()); QString error;
            pcv::camera::CaptureConfiguration config{pcv::camera::CaptureBoundaryMode::SdkImageFrame,0,true};
            config.storeRawDepth = true; f->boundarySource = pcv::camera::ScanBoundarySource::SdkImageFrame;
            const auto session = pcv::camera::createCaptureSession(roundtrip.path(),config,"SDK golden fixture",&error);
            CHECK(!session.isEmpty());
            pcv::camera::CaptureRecorder recorder(session,config); CHECK(recorder.queue()->push(f)); recorder.finish();
            CHECK(recorder.savedFrames().size() == 1);
            const auto loaded = pcv::camera::readCapturedFrame(recorder.savedFrames().first().directory,{},pcv::camera::CaptureFrameReadMode::GeometryOnly);
            CHECK(loaded.ok && loaded.rawFrame && loaded.rawFrame->data == f->data && loaded.rawFrame->intensityData == f->intensityData);
            CHECK(loaded.sourceIndices == original.sourceIndices && loaded.points.size() == original.points.size());
            CHECK(std::memcmp(original.points.constData(),loaded.points.constData(),size_t(original.points.size())*sizeof(pointcloud::Point3D)) == 0);
            std::cout << "SDK golden XYZ/raster and lossless disk roundtrip passed" << std::endl;
        }
        CameraDepthPreviewV1 view; view.resize(960,720); view.show(); view.setFrame(f);
        QApplication::processEvents();
        CHECK(!view.m_prepared && !view.m_worker && view.m_frame == f);
        auto *canvas = view.m_canvas;
        // Native cursor enter/leave events must not replace the scripted stable
        // hover used for reproducible end-to-end timing.
        canvas->hovered = {};
        CHECK(canvas->pixelAt({-1,-1}) == QPoint(-1,-1));
        const QPoint center = canvas->pixelAt(QPointF(canvas->width()/2.0,canvas->height()/2.0));
        CHECK(center.x()>=0 && center.y()>=0);
        if (!pcv::camera::cameraConversionEnvironment().error.isEmpty()) {
            view.convert(false,false); CHECK(waitUntil([&]{return !view.m_worker;})); CHECK(!view.m_prepared);
            view.cancelAndWait(); CHECK(!view.m_frame && !view.m_latest); return 0;
        }
        const int repeats = benchmark && args.contains("--repeats") ? args.value(args.indexOf("--repeats")+1).toInt() : 1;
        auto memory = [](const char *stage) {
            const auto usage = pcv::view::readProcessMemoryUsage();
            std::cout << "memory stage=" << stage << " working_set=" << usage.workingSetBytes
                << " private_commit=" << usage.privateCommitBytes << std::endl;
        };
        memory("depth_browsing");
        for (int repeat=0;repeat<repeats;++repeat) {
            view.toggleSelection(); CHECK(view.m_selecting && !view.m_prepared);
            auto newer = std::make_shared<pcv::camera::CameraFrame>(*f); newer->frameNumber++;
            view.setFrame(newer); CHECK(view.m_frame == f);
            QElapsedTimer timer; timer.start();
            view.hover(center);
            if (!waitUntil([&]{return !view.m_worker && (!view.m_candidateMask.isNull() || !view.m_debounce->isActive());},benchmark ? 180000 : 30000)) {
                std::cerr << "timeout: " << view.m_status->text().toStdString() << std::endl; return 1;
            }
            if (view.m_candidateMask.isNull()) { std::cerr << view.m_status->text().toStdString() << std::endl; return 1; }
            QApplication::processEvents();
            canvas->repaint();
            std::cout << "highlight width=" << f->width << " height=" << f->height << " trial=" << repeat+1 << " total_ms=" << timer.elapsed() << std::endl;
            const auto &diag = view.m_lastDiagnostics;
            std::cout << "conversion_ms=" << view.m_conversionMs << " index_ms=" << view.m_indexMs << " selection_ms=" << view.m_selectionMs
                << " ransac_ms=" << diag.timings.ransacMs << " candidate_ms=" << diag.timings.candidateMs
                << " classification_ms=" << diag.timings.classificationMs << " projection_ms=" << diag.projectionMs
                << " connectivity_ms=" << diag.timings.connectivityMs << " gap_ms=" << diag.gapMergeMs
                << " refinement_iterations=" << diag.refinementIterations << std::endl;
            if (benchmark) CHECK(view.grab().save(QDir(fixture).filePath("depth-selection-review.png")));
            memory("highlight");
            CHECK(view.m_prepared && view.m_prepared->points.size() == view.m_prepared->indices.size());
            if (benchmark && repeat == 0) {
                const auto originalMask = view.m_candidateMask;
                timer.restart(); view.hover(center+QPoint(1,0)); canvas->repaint();
                CHECK(!view.m_worker && view.m_candidateMask == originalMask);
                std::cout << "same_region_reuse_ms=" << timer.elapsed() << std::endl;
                // Isolate a complete new selection request with an existing
                // index, including debounce and painting, from region reuse.
                view.m_candidateMask = {}; timer.restart(); view.hover(center);
                CHECK(waitUntil([&]{return !view.m_worker && !view.m_candidateMask.isNull();},180000));
                canvas->repaint();
                std::cout << "existing_index_full_selection_ms=" << timer.elapsed() << std::endl;
                CHECK(view.m_candidateMask == originalMask);
            }
            canvas->clicked(center); CHECK(view.m_locked);
            view.m_confirm->click(); CHECK(!view.m_confirmedMask.isNull());
            if (!benchmark) {
                QTemporaryDir selections; CHECK(selections.isValid());
                view.saveSelectionTo(selections.path());
                const auto folders = QDir(selections.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
                CHECK(folders.size() == 1);
                const QDir selectionDir(QDir(selections.path()).filePath(folders.first()));
                CHECK(QImage(selectionDir.filePath("mask.png")).convertToFormat(QImage::Format_Grayscale8) == view.m_confirmedMask);
                const auto saved = QJsonDocument::fromJson(bytes(selectionDir.filePath("selection.json"))).object();
                CHECK(saved.value("schema") == "pcv-camera-plane-selection-v1" && saved.value("source_fingerprint").toString().size() == 64);
                qsizetype published = 0;
                view.publishCloud = [&published](const QVector<pointcloud::Point3D> &points){published=points.size();};
                view.convert(true,false); CHECK(waitUntil([&]{return !view.m_worker;})); CHECK(published>0);
                // A confirmed mask survives releasing the cloud/index while
                // the displayed source stays unchanged.
                view.setFrame(f);
                view.toggleSelection(); view.toggleSelection();
                CHECK(!view.m_prepared && !view.m_confirmedMask.isNull());
                view.convert(true,false); CHECK(waitUntil([&]{return !view.m_worker;})); CHECK(published>0);
            }
            if (view.m_locked) view.toggleSelection();
            if (view.m_selecting) view.toggleSelection();
            CHECK(!view.m_selecting && !view.m_prepared && !view.m_locked);
            view.setFrame(f); CHECK(view.m_frame == f);
            memory("released");
        }
        view.toggleSelection(); view.hover(center); view.schedule();
        view.cancelAndWait(); QApplication::processEvents();
        CHECK(!view.m_worker && !view.m_prepared && !view.m_frame && !view.m_latest);
        return 0;
    }
};
int main(int argc,char **argv) { QApplication app(argc,argv); return CameraDepthPreviewTestAccess::run(app.arguments()); }
