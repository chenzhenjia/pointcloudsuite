#include <pcv/interface/temp_workpiece_interface.h>
#include <pcv/planefitting/plane_fitting.h>
#include <pcv/roi/temp_workpiece_geometry.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QVector4D>

#include <atomic>
#include <csignal>
#include <cmath>
#include <iostream>
#include <limits>

#ifndef PCV_PROJECT_VERSION
#define PCV_PROJECT_VERSION "unknown"
#endif

namespace {
constexpr int kUsageError = 2, kInputError = 10, kTransformError = 20,
              kPlaneError = 30, kOutputError = 40, kCancelled = 130;
std::atomic_bool g_cancelled{false};

void signalHandler(int) { g_cancelled.store(true, std::memory_order_relaxed); }

void usage(std::ostream &out)
{
    out << "Usage: pointcloudcli [--help] [--version]\n"
        << "Usage:\n"
        << "  pointcloudcli --help\n"
        << "  pointcloudcli --version\n"
        << "  pointcloudcli temp-workpiece run <job.json> [--allow-overwrite]\n"
        << "Options:\n"
        << "  --help     Show this help message.\n"
        << "  --version  Show the PointCloudSuite version.\n"
        << "  --allow-overwrite  Allow replacing existing output files.\n";
}

void progress(int n, const char *stage) { std::cerr << "[" << n << "/8] " << stage << '\n'; }

bool finiteNumber(const QJsonValue &value, double *number)
{
    if (!value.isDouble()) return false;
    const double v = value.toDouble();
    if (!std::isfinite(v)) return false;
    if (number) *number = v;
    return true;
}

QString requiredString(const QJsonValue &value, const QString &name, QString *error)
{
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        if (error) *error = name + QStringLiteral(" must be a non-empty string");
        return {};
    }
    return value.toString().trimmed();
}

bool parseIndices(const QJsonValue &value, int required, QVector<int> *indices,
                  QString *error, const QString &name)
{
    if (!value.isArray() || (required >= 0 && value.toArray().size() != required)) {
        if (error) *error = name + QStringLiteral(" must contain exactly %1 indices").arg(required);
        return false;
    }
    QVector<int> parsed;
    for (const auto item : value.toArray()) {
        double number = 0.0;
        if (!finiteNumber(item, &number) || number < 0.0
            || number > double(std::numeric_limits<int>::max()) || std::floor(number) != number) {
            if (error) *error = name + QStringLiteral(" contains an invalid index");
            return false;
        }
        const int index = int(number);
        if (parsed.contains(index)) {
            if (error) *error = name + QStringLiteral(" contains duplicate indices");
            return false;
        }
        parsed.push_back(index);
    }
    if (indices) *indices = parsed;
    return true;
}

bool safeComponent(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")) return false;
    for (const QChar c : QStringLiteral("<>:\"|?*/\\")) if (text.contains(c)) return false;
    return !text.contains(QStringLiteral("..")) && !text.endsWith(QLatin1Char('.'))
        && !text.endsWith(QLatin1Char(' ')) && !QFileInfo(text).isAbsolute();
}

bool resolveUnder(const QString &base, const QString &input, QString *resolved, QString *error)
{
    const QFileInfo info(input);
    const QString path = info.isAbsolute()
        ? QDir::cleanPath(info.absoluteFilePath())
        : QDir::cleanPath(QFileInfo(QDir(base).filePath(input)).absoluteFilePath());
    const QString root = QDir::cleanPath(QFileInfo(base).absoluteFilePath());
    if (!(path.compare(root, Qt::CaseInsensitive) == 0
          || path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))) {
        if (error) *error = QStringLiteral("path escapes job directory: %1").arg(input);
        return false;
    }
    if (resolved) *resolved = path;
    return true;
}

struct Job {
    QString jobId, scanningInfo, calibrationOverride, outputDirectory, roiMode;
    bool allowOverwrite = false;
    QVector<int> seedIndices, verificationIndices, planeIndices, roiIndices;
    float angleTolerance = 1.0f, distanceTolerance = 0.4f;
    float roiWidth = 1000.0f, roiHeight = 800.0f;
};

bool parseJob(const QString &filePath, bool allowOverwriteFlag, Job *job, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("failed to open job JSON"); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("invalid job JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toString() != QStringLiteral("pointcloudcli-temp-workpiece-job-v1")
        || root.value(QStringLiteral("kind")).toString() != QStringLiteral("temp_workpiece_job")) {
        if (error) *error = QStringLiteral("unsupported schema_version or kind");
        return false;
    }
    Job parsed;
    parsed.jobId = requiredString(root.value(QStringLiteral("job_id")), QStringLiteral("job_id"), error);
    if (parsed.jobId.isEmpty() || !safeComponent(parsed.jobId)) { if (error) *error = QStringLiteral("job_id is not a safe path component"); return false; }
    const QString base = QFileInfo(filePath).absolutePath();
    const QString scan = requiredString(root.value(QStringLiteral("scanning_info_file")), QStringLiteral("scanning_info_file"), error);
    if (scan.isEmpty() || !resolveUnder(base, scan, &parsed.scanningInfo, error)) return false;
    const QString output = requiredString(root.value(QStringLiteral("output_directory")), QStringLiteral("output_directory"), error);
    if (output.isEmpty() || !resolveUnder(base, output, &parsed.outputDirectory, error)) return false;
    if (root.contains(QStringLiteral("calibration_override_file"))
        && !root.value(QStringLiteral("calibration_override_file")).isString()) {
        if (error) *error = QStringLiteral("calibration_override_file must be a string");
        return false;
    }
    const QString calibration = root.value(QStringLiteral("calibration_override_file")).toString().trimmed();
    if (!calibration.isEmpty() && !resolveUnder(base, calibration, &parsed.calibrationOverride, error)) return false;
    if (root.contains(QStringLiteral("allow_overwrite"))
        && !root.value(QStringLiteral("allow_overwrite")).isBool()) {
        if (error) *error = QStringLiteral("allow_overwrite must be boolean");
        return false;
    }
    parsed.allowOverwrite = allowOverwriteFlag
        || root.value(QStringLiteral("allow_overwrite")).toBool(false);
    const QJsonObject plane = root.value(QStringLiteral("plane")).toObject();
    if (plane.isEmpty()) { if (error) *error = QStringLiteral("plane object is required"); return false; }
    if (!parseIndices(plane.value(QStringLiteral("seed_indices")), 3, &parsed.seedIndices, error, QStringLiteral("seed_indices"))
        || !parseIndices(plane.value(QStringLiteral("verification_indices")), 3, &parsed.verificationIndices, error, QStringLiteral("verification_indices"))) return false;
    if (plane.contains(QStringLiteral("plane_indices"))) {
        const QJsonValue planeIndices = plane.value(QStringLiteral("plane_indices"));
        if (!planeIndices.isArray()) {
            if (error) *error = QStringLiteral("plane_indices must be an array");
            return false;
        }
        if (!planeIndices.toArray().isEmpty()
            && !parseIndices(planeIndices, -1, &parsed.planeIndices, error,
                             QStringLiteral("plane_indices"))) return false;
    }
    double number = 0.0;
    if (plane.contains(QStringLiteral("angle_tolerance_deg")) && (!finiteNumber(plane.value(QStringLiteral("angle_tolerance_deg")), &number) || number < 0.0)) { if (error) *error = QStringLiteral("angle_tolerance_deg is invalid"); return false; }
    if (plane.contains(QStringLiteral("angle_tolerance_deg"))) parsed.angleTolerance = float(number);
    if (plane.contains(QStringLiteral("distance_tolerance_mm")) && (!finiteNumber(plane.value(QStringLiteral("distance_tolerance_mm")), &number) || number <= 0.0)) { if (error) *error = QStringLiteral("distance_tolerance_mm is invalid"); return false; }
    if (plane.contains(QStringLiteral("distance_tolerance_mm"))) parsed.distanceTolerance = float(number);
    const QJsonObject roi = root.value(QStringLiteral("roi")).toObject();
    if (roi.isEmpty()) { if (error) *error = QStringLiteral("roi object is required"); return false; }
    parsed.roiMode = roi.value(QStringLiteral("mode")).toString();
    if (parsed.roiMode != QStringLiteral("indices") && parsed.roiMode != QStringLiteral("rectangle")) { if (error) *error = QStringLiteral("roi.mode must be indices or rectangle"); return false; }
    if (parsed.roiMode == QStringLiteral("indices")) {
        if (!parseIndices(roi.value(QStringLiteral("roi_indices")), -1, &parsed.roiIndices, error, QStringLiteral("roi_indices")) || parsed.roiIndices.isEmpty()) return false;
    } else if (roi.contains(QStringLiteral("roi_indices"))
               && !roi.value(QStringLiteral("roi_indices")).isArray()) {
        if (error) *error = QStringLiteral("roi_indices must be an array");
        return false;
    }
    if (roi.contains(QStringLiteral("width_mm")) && (!finiteNumber(roi.value(QStringLiteral("width_mm")), &number) || number <= 0.0)) { if (error) *error = QStringLiteral("roi.width_mm is invalid"); return false; }
    if (roi.contains(QStringLiteral("width_mm"))) parsed.roiWidth = float(number);
    if (roi.contains(QStringLiteral("height_mm")) && (!finiteNumber(roi.value(QStringLiteral("height_mm")), &number) || number <= 0.0)) { if (error) *error = QStringLiteral("roi.height_mm is invalid"); return false; }
    if (roi.contains(QStringLiteral("height_mm"))) parsed.roiHeight = float(number);
    *job = parsed;
    return true;
}

QJsonObject failure(const QString &code, const QString &stage, const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error_code"), code},
            {QStringLiteral("stage"), stage}, {QStringLiteral("message"), message}};
}

int emitFailure(const QString &code, const QString &stage, const QString &message, int exitCode)
{
    std::cerr << QJsonDocument(failure(code, stage, message)).toJson(QJsonDocument::Compact).constData() << '\n';
    return exitCode;
}

int runJob(const QString &jobPath, bool allowOverwrite)
{
    progress(1, "parse_job");
    Job job; QString error;
    if (!parseJob(jobPath, allowOverwrite, &job, &error)) return emitFailure("PCV_CONTRACT_001", "parse_job", error, kUsageError);
    if (g_cancelled.load()) return emitFailure("PCV_CANCELLED", "parse_job", "cancelled", kCancelled);
    progress(2, "prepare_scan");
    pcv::interface::TempWorkpieceOptions prepareOptions;
    prepareOptions.jobId = job.jobId; prepareOptions.scanningInfoPath = job.scanningInfo;
    prepareOptions.calibrationOverridePath = job.calibrationOverride;
    // The CLI resolves output_directory relative to the Job JSON. Finalize
    // receives that already-resolved path; leaving this optional preparation
    // field empty avoids reinterpreting it relative to the scan JSON.
    prepareOptions.isCancelled = [] { return g_cancelled.load(std::memory_order_relaxed); };
    auto prepared = pcv::interface::prepareTempWorkpiece(prepareOptions, &error);
    if (!prepared.success) {
        if (prepared.errorCode == QStringLiteral("PCV_CANCELLED"))
            return emitFailure("PCV_CANCELLED", "prepare_scan", "cancelled", kCancelled);
        int code = kInputError;
        if (prepared.errorCode.startsWith(QStringLiteral("PCV_TRANSFORM"))) code = kTransformError;
        else if (prepared.errorCode.startsWith(QStringLiteral("PCV_OUTPUT"))) code = kOutputError;
        return emitFailure(prepared.errorCode, "prepare_scan", error, code);
    }
    if (g_cancelled.load()) return emitFailure("PCV_CANCELLED", "prepare_scan", "cancelled", kCancelled);
    progress(3, "fit_plane");
    pcv::planefitting::Options fitOptions;
    fitOptions.initialTolerance = 1.0f; fitOptions.surfaceTolerance = job.distanceTolerance; fitOptions.minInliers = 3;
    fitOptions.isCancelled = [] { return g_cancelled.load(std::memory_order_relaxed); };
    const auto fitted = pcv::planefitting::fit(prepared.robotBasePoints, job.seedIndices, fitOptions);
    if (g_cancelled.load()) return emitFailure("PCV_CANCELLED", "fit_plane", "cancelled", kCancelled);
    if (!fitted.ok) return emitFailure("PCV_PLANE_001", "fit_plane", fitted.error, kPlaneError);
    if (g_cancelled.load()) return emitFailure("PCV_CANCELLED", "fit_plane", "cancelled", kCancelled);
    progress(4, "verify_plane");
    const auto consistency = pcv::planefitting::validateConsistency(prepared.robotBasePoints, fitted.model,
        job.seedIndices, job.verificationIndices, job.angleTolerance, job.distanceTolerance,
        [] { return g_cancelled.load(std::memory_order_relaxed); });
    if (consistency.cancelled || g_cancelled.load())
        return emitFailure("PCV_CANCELLED", "plane_verification", "cancelled", kCancelled);
    if (!consistency.passed) return emitFailure("PCV_PLANE_001", "plane_verification", consistency.error.isEmpty() ? QStringLiteral("verification failed") : consistency.error, kPlaneError);
    QVector<int> planeIndices = job.planeIndices.isEmpty() ? fitted.planeIndices : job.planeIndices;
    for (int index : planeIndices) if (index < 0 || index >= prepared.robotBasePoints.size()) return emitFailure("PCV_PLANE_001", "fit_plane", "plane index is outside robot_base cloud", kPlaneError);
    if (planeIndices.size() < 3) return emitFailure("PCV_PLANE_001", "fit_plane", "plane_indices must contain at least 3 points", kPlaneError);
    progress(5, "build_wobj1");
    const auto center = pcv::planefitting::calculateBoundsCenter(prepared.robotBasePoints, planeIndices);
    if (!center.ok) return emitFailure("PCV_FRAME_001", "build_wobj1", center.error, kPlaneError);
    const auto selected = pcv::roi::selectAutomaticAxisPoints(prepared.robotBasePoints, planeIndices, center.center,
        QVector3D(fitted.model.a, fitted.model.b, fitted.model.c), job.distanceTolerance);
    if (!selected.ok) return emitFailure("PCV_FRAME_001", "build_wobj1", selected.error, kPlaneError);
    const auto frame = pcv::roi::buildWorkpieceCoordinateSystemFromThreePoints(prepared.robotBasePoints, center.center,
        selected.xPointIndex, selected.yPointIndex);
    if (!frame.valid) return emitFailure("PCV_FRAME_001", "build_wobj1", frame.error, kPlaneError);
    progress(6, "generate_roi");
    const QVector<int> explicitRoi = job.roiMode == QStringLiteral("indices") ? job.roiIndices : QVector<int>{};
    if (!explicitRoi.isEmpty()) for (int index : explicitRoi) if (index < 0 || index >= prepared.robotBasePoints.size()) return emitFailure("PCV_PLANE_001", "generate_roi", "roi index is outside robot_base cloud", kPlaneError);
    const auto mask = pcv::roi::rasterizePlaneMask(prepared.robotBasePoints, planeIndices,
        QVector4D(fitted.model.a, fitted.model.b, fitted.model.c, fitted.model.d), frame,
        job.roiWidth, job.roiHeight, 0.05f, explicitRoi, job.distanceTolerance,
        [] { return g_cancelled.load(std::memory_order_relaxed); });
    if (mask.cancelled || g_cancelled.load())
        return emitFailure("PCV_CANCELLED", "generate_roi", "cancelled", kCancelled);
    if (!mask.ok) return emitFailure("PCV_IMAGE_001", "generate_roi", mask.error, kPlaneError);
    if (g_cancelled.load()) return emitFailure("PCV_CANCELLED", "generate_roi", "cancelled", kCancelled);
    progress(7, "write_outputs");
    pcv::interface::TempWorkpieceFinalizeOptions finalize;
    finalize.outputDirectory = job.outputDirectory; finalize.allowOverwrite = job.allowOverwrite;
    finalize.planeIndices = planeIndices; finalize.roiIndices = mask.roiIndices; finalize.planeMask = mask.image;
    finalize.planeModel = QVector4D(fitted.model.a, fitted.model.b, fitted.model.c, fitted.model.d);
    finalize.rmsErrorMm = fitted.rmsError; finalize.planeDistanceToleranceMm = job.distanceTolerance;
    finalize.originInRobotBase = frame.originInRobotBase; finalize.axisXInRobotBase = frame.axisXInRobotBase;
    finalize.axisYInRobotBase = frame.axisYInRobotBase; finalize.axisZInRobotBase = frame.axisZInRobotBase;
    finalize.abcDeg = QVector3D(frame.poseA, frame.poseB, frame.poseC);
    finalize.TBaseWorkpiece = frame.workpieceToRobotBase; finalize.TWorkpieceBase = frame.robotBaseToWorkpiece;
    finalize.isCancelled = [] { return g_cancelled.load(std::memory_order_relaxed); };
    auto result = pcv::interface::finalizeTempWorkpiece(prepared, finalize, &error);
    if (result.errorCode == QStringLiteral("PCV_CANCELLED") || g_cancelled.load())
        return emitFailure("PCV_CANCELLED", "write_outputs", "cancelled", kCancelled);
    if (!result.success) return emitFailure(result.errorCode, "write_outputs", error, kOutputError);
    progress(8, "completed");
    const QJsonObject outputs{{QStringLiteral("baseline_robot_base_ply"), QDir::fromNativeSeparators(result.baselineRobotBasePly)},
        {QStringLiteral("roi_template_robot_base_ply"), QDir::fromNativeSeparators(result.roiTemplateRobotBasePly)},
        {QStringLiteral("plane_mask_png"), QDir::fromNativeSeparators(result.planeMaskPng)},
        {QStringLiteral("temp_workpiece_info_json"), QDir::fromNativeSeparators(result.tempWorkpieceInfoPath)}};
    const QJsonObject counts{{QStringLiteral("input_points"), int(prepared.robotBasePoints.size())},
        {QStringLiteral("plane_points"), int(result.planePointCount)}, {QStringLiteral("roi_points"), int(result.roiPointCount)}};
    const QJsonObject wobj{{QStringLiteral("origin_xyz"), QJsonArray{frame.originInRobotBase.x(), frame.originInRobotBase.y(), frame.originInRobotBase.z()}},
        {QStringLiteral("abc_deg"), QJsonArray{frame.poseA, frame.poseB, frame.poseC}}};
    const QJsonObject summary{{QStringLiteral("ok"), true}, {QStringLiteral("command"), QStringLiteral("temp-workpiece run")},
        {QStringLiteral("job_id"), job.jobId}, {QStringLiteral("outputs"), outputs}, {QStringLiteral("counts"), counts},
        {QStringLiteral("wobj1"), wobj}, {QStringLiteral("warnings"), prepared.warning.isEmpty() ? QJsonArray{} : QJsonArray{prepared.warning}}};
    std::cout << QJsonDocument(summary).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    g_cancelled.store(false, std::memory_order_relaxed);
    std::signal(SIGINT, signalHandler);
    if (argc == 1) { std::cerr << "error: missing command-line option\n"; usage(std::cerr); return kUsageError; }
    const QString first = QString::fromLocal8Bit(argv[1]);
    if (argc == 2 && first == QStringLiteral("--help")) { usage(std::cout); return 0; }
    if (argc == 2 && first == QStringLiteral("--version")) { std::cout << "pointcloudcli " << PCV_PROJECT_VERSION << '\n'; return 0; }
    if (argc == 2 && first.startsWith(QStringLiteral("--"))) {
        std::cerr << "error: unknown option: " << first.toStdString() << '\n';
        usage(std::cerr);
        return kUsageError;
    }
    if (first != QStringLiteral("temp-workpiece") || argc < 4 || QString::fromLocal8Bit(argv[2]) != QStringLiteral("run"))
        return emitFailure("PCV_CONTRACT_001", "parse_command", "unknown command or missing job.json", kUsageError);
    bool allowOverwrite = false; QString jobPath;
    for (int i = 3; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--allow-overwrite")) allowOverwrite = true;
        else if (jobPath.isEmpty()) jobPath = arg;
        else return emitFailure("PCV_CONTRACT_001", "parse_command", "unknown option: " + arg, kUsageError);
    }
    if (jobPath.isEmpty()) return emitFailure("PCV_CONTRACT_001", "parse_command", "job.json is required", kUsageError);
    return runJob(QFileInfo(jobPath).absoluteFilePath(), allowOverwrite);
}
