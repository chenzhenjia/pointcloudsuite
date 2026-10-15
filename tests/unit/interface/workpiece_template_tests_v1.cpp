#include <pcv/workpiece/workpiece_template_v1.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed at " << __LINE__ \
    << ": " << #condition << '\n'; return false; } } while (false)

using namespace pcv::workpiece;

namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
SaveWorkpieceTemplateRequest requestFor(const QString &root)
{
    SaveWorkpieceTemplateRequest request;
    request.name = QString::fromUtf8("工件 A:基准");
    request.outputRoot = root;
    request.coordinateFrame = QStringLiteral("robot_base");
    request.points = {{1.25f, -2.5f, 0.0f, 0, 0, 0},
        {-10, 20, 30, 0.25f, -0.5f, 1}, {3, 2, -1, -1, 0, 0}};
    request.source = {{QStringLiteral("entry"), QStringLiteral("handeye_conversion")},
        {QStringLiteral("files"), QJsonArray{QStringLiteral("Z:/missing original.ply")}}};
    return request;
}
bool roundTripAndVersions()
{
    QTemporaryDir temp;
    CHECK(temp.isValid());
    auto request = requestFor(temp.path() + QString::fromUtf8("/中文 空格"));
    const auto shared = request.points;
    const auto saved = saveWorkpieceTemplate(request);
    CHECK(saved.success && !saved.cancelled && saved.points.isEmpty());
    CHECK(saved.info.name == request.name && !saved.info.versionId.isEmpty());
    CHECK(saved.info.minimum == QVector3D(-10, -2.5f, -1));
    CHECK(saved.info.maximum == QVector3D(3, 20, 30));
    CHECK(QFileInfo(saved.outputDirectory).fileName().contains(QStringLiteral("_v1_")));
    CHECK(!QFileInfo(saved.outputDirectory).fileName().contains(QLatin1Char(':')));
    const auto loaded = loadWorkpieceTemplate({saved.infoFile});
    CHECK(loaded.success && loaded.info.source == request.source);
    CHECK(loaded.points.size() == shared.size());
    for (qsizetype i = 0; i < shared.size(); ++i)
        CHECK(std::memcmp(&loaded.points.at(i), &shared.at(i), sizeof(pointcloud::Point3D)) == 0);
    const auto bytes = readFile(saved.pointCloudFile);
    const auto second = saveWorkpieceTemplate(request);
    CHECK(second.success && second.outputDirectory != saved.outputDirectory);
    CHECK(readFile(saved.pointCloudFile) == bytes);
    // Moving the entire package does not require the original source files.
    const QString moved = temp.path() + QString::fromUtf8("/搬迁 后模板");
    CHECK(QDir().rename(saved.outputDirectory, moved));
    CHECK(loadWorkpieceTemplate({moved + "/workpiece_template_info.json"}).success);
    request.name = QStringLiteral("CON");
    const auto reserved = saveWorkpieceTemplate(request);
    CHECK(reserved.success && QFileInfo(reserved.outputDirectory).fileName().startsWith("_CON"));
    return true;
}
bool invalidInputsAndPackages()
{
    QTemporaryDir temp;
    auto request = requestFor(temp.path());
    request.coordinateFrame = QStringLiteral("camera");
    CHECK(!saveWorkpieceTemplate(request).success);
    request.coordinateFrame = QStringLiteral("robot_base");
    const auto original = request.points;
    request.points.clear();
    CHECK(!saveWorkpieceTemplate(request).success);
    request.points = original;
    request.points[0].nx = std::numeric_limits<float>::quiet_NaN();
    CHECK(!saveWorkpieceTemplate(request).success);
    request.points = original;
    request.points[0].z = std::numeric_limits<float>::infinity();
    CHECK(!saveWorkpieceTemplate(request).success);
    request.points = original;
    request.name = "   ";
    CHECK(!saveWorkpieceTemplate(request).success);
    request.name = "fixture";
    // Simulate a root creation failure without relying on administrator permissions.
    request.outputRoot = temp.path() + "/root_file";
    CHECK(writeFile(request.outputRoot, "keep"));
    CHECK(!saveWorkpieceTemplate(request).success);
    request.outputRoot = QString::fromUtf8(PCV_TEST_SOURCE_DIR);
    CHECK(!saveWorkpieceTemplate(request).success);
    request.outputRoot = temp.path();
    const auto saved = saveWorkpieceTemplate(request);
    CHECK(saved.success);
    const QByteArray jsonBytes = readFile(saved.infoFile);
    const auto object = QJsonDocument::fromJson(jsonBytes).object();
    const auto rejectEdit = [&](const QString &key, const QJsonValue &value) {
        auto edited = object;
        edited.insert(key, value);
        return writeFile(saved.infoFile, QJsonDocument(edited).toJson())
            && !loadWorkpieceTemplate({saved.infoFile}).success;
    };
    CHECK(rejectEdit("schema_version", "future-version"));
    CHECK(rejectEdit("coordinate_frame", "camera"));
    CHECK(rejectEdit("units", "m"));
    CHECK(rejectEdit("point_count", 4));
    CHECK(rejectEdit("point_count", 2.5));
    CHECK(rejectEdit("point_cloud_file", "../escape.ply"));
    CHECK(rejectEdit("point_cloud_file", saved.pointCloudFile));
    CHECK(rejectEdit("bounds_mm", QJsonObject{{"minimum", QJsonArray{0, 0, 0}},
                                             {"maximum", QJsonArray{1, 1, 1}}}));
    CHECK(writeFile(saved.infoFile, "{invalid json"));
    CHECK(!loadWorkpieceTemplate({saved.infoFile}).success);
    CHECK(writeFile(saved.infoFile, jsonBytes));
    QByteArray ply = readFile(saved.pointCloudFile);
    CHECK(writeFile(saved.pointCloudFile, ply.left(ply.size() - 1)));
    CHECK(!loadWorkpieceTemplate({saved.infoFile}).success);
    CHECK(writeFile(saved.pointCloudFile, ply));
    const qsizetype payload = ply.indexOf("end_header\n") + qsizetype(strlen("end_header\n"));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(ply.data() + payload + 3 * sizeof(float), &nan, sizeof(nan));
    CHECK(writeFile(saved.pointCloudFile, ply));
    const auto invalidNormal = loadWorkpieceTemplate({saved.infoFile});
    CHECK(!invalidNormal.success && invalidNormal.points.isEmpty());
    CHECK(QFile::remove(saved.pointCloudFile));
    CHECK(!loadWorkpieceTemplate({saved.infoFile}).success);
    return true;
}
bool cancellationAndRollback()
{
    QTemporaryDir temp;
    auto request = requestFor(temp.path());
    bool cancel = true;
    request.isCancelled = [&] { return cancel; };
    CHECK(saveWorkpieceTemplate(request).cancelled);
    cancel = false;
    request.progress = [&](WorkpieceTemplateStage stage, qsizetype, qsizetype) {
        if (stage == WorkpieceTemplateStage::WritingMetadata) cancel = true;
    };
    const auto stopped = saveWorkpieceTemplate(request);
    CHECK(!stopped.success && stopped.cancelled);
    CHECK(QDir(temp.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    cancel = false;
    request.progress = {};
    const auto saved = saveWorkpieceTemplate(request);
    CHECK(saved.success);
    LoadWorkpieceTemplateRequest load{saved.infoFile};
    load.isCancelled = [&] { return cancel; };
    load.progress = [&](WorkpieceTemplateStage stage, qsizetype, qsizetype) {
        if (stage == WorkpieceTemplateStage::ReadingCloud) cancel = true;
    };
    const auto cancelledRead = loadWorkpieceTemplate(load);
    CHECK(cancelledRead.cancelled && cancelledRead.points.isEmpty());
    cancel = false;
    // Cancellation only becomes true once both final files exist: commit wins.
    request.isCancelled = [&] {
        for (const QString &dir : QDir(temp.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QDir(temp.path()).filePath(dir) == saved.outputDirectory) continue;
            const QDir version(QDir(temp.path()).filePath(dir));
            if (QFileInfo::exists(version.filePath(kWorkpieceTemplatePly))
                && QFileInfo::exists(version.filePath(kWorkpieceTemplateJson))) return true;
        }
        return false;
    };
    const auto late = saveWorkpieceTemplate(request);
    CHECK(late.success && !late.cancelled && request.isCancelled());
    // A commit collision must restore the transaction and preserve the colliding file.
    request.isCancelled = {};
    QString interferingDirectory;
    bool inserted = false;
    request.progress = [&](WorkpieceTemplateStage stage, qsizetype, qsizetype) {
        if (stage != WorkpieceTemplateStage::Committing) return;
        for (const QString &dir : QDir(temp.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path = QDir(temp.path()).filePath(dir);
            if (path == saved.outputDirectory || path == late.outputDirectory) continue;
            interferingDirectory = path;
            inserted = writeFile(QDir(path).filePath(kWorkpieceTemplateJson), "sentinel");
        }
    };
    const auto collision = saveWorkpieceTemplate(request);
    CHECK(inserted && !collision.success && !collision.cancelled);
    CHECK(!QFileInfo::exists(QDir(interferingDirectory).filePath(kWorkpieceTemplatePly)));
    CHECK(readFile(QDir(interferingDirectory).filePath(kWorkpieceTemplateJson)) == "sentinel");
    return true;
}

quint64 peakWorkingSet()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
        return memory.PeakWorkingSetSize;
#endif
    return 0;
}
bool benchmark(qsizetype count)
{
    QTemporaryDir temp;
    auto request = requestFor(temp.path());
    request.points.resize(count);
    for (qsizetype i = 0; i < count; ++i)
        request.points[i] = {float(i % 10000) * 0.1f, float(i / 10000) * 0.1f,
            float(i % 37) * 0.01f, 0, 0, 1};
    const auto shared = request.points;
    request.isCancelled = [] { return false; }; // Exercise the cancellable writer used by GUI.
    QElapsedTimer timer;
    timer.start();
    const auto saved = saveWorkpieceTemplate(request);
    const qint64 saveMs = timer.elapsed();
    CHECK(saved.success);
    timer.restart();
    const auto loaded = loadWorkpieceTemplate({saved.infoFile});
    const qint64 loadMs = timer.elapsed();
    CHECK(loaded.success && loaded.points.size() == count);
    CHECK(std::memcmp(shared.constData(), loaded.points.constData(), size_t(count) * sizeof(pointcloud::Point3D)) == 0);
    std::atomic_bool cancel{false};
    std::thread cancelling;
    request.isCancelled = [&] { return cancel.load(); };
    request.progress = [&](WorkpieceTemplateStage stage, qsizetype, qsizetype) {
        if (stage == WorkpieceTemplateStage::WritingCloud) {
            cancelling = std::thread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                timer.restart();
                cancel.store(true);
            });
        }
    };
    const auto stopped = saveWorkpieceTemplate(request);
    if (cancelling.joinable()) cancelling.join();
    CHECK(stopped.cancelled && !stopped.success);
    std::cout << "points=" << count << " save_ms=" << saveMs << " load_ms=" << loadMs
        << " cancel_during_write_ms=" << timer.elapsed() << " peak_working_set_bytes=" << peakWorkingSet() << '\n';
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments().at(1) == "--benchmark")
        return benchmark(app.arguments().at(2).toLongLong()) ? 0 : 1;
    if (!roundTripAndVersions() || !invalidInputsAndPackages() || !cancellationAndRollback()) return 1;
    std::cout << "Workpiece template v1 regression passed\n";
    return 0;
}
