#include <pcv/interface/process_job_json.h>
#include <pcv/camera/capture_session_v1.h>
#include <pcv/io/artifact_transaction.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <cmath>

namespace pcv::interface {
namespace {
constexpr auto executionSchema = "pcv-execution-result-v1";
constexpr auto paramSchema = "pcv-ply-process-param-v1";
constexpr auto templateSchema = "pcv-workpiece-template-process-v1";
constexpr auto summarySchema = "pcv-execution-result-summary-v1";

bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool inside(const QString &path, const QString &root) {
    const QString p = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const QString r0 = QDir::fromNativeSeparators(QDir::cleanPath(root));
    const QString r = r0.endsWith('/') ? r0.chopped(1) : r0;
    return p.compare(r, Qt::CaseInsensitive) == 0
        || p.startsWith(r + '/', Qt::CaseInsensitive);
}
// Canonicalize the existing ancestor too, so a not-yet-created output cannot
// escape through a junction or symlink in its parent path.
QString canonicalDestination(QString path) {
    QStringList tail;
    while (!QFileInfo::exists(path)) {
        const QFileInfo info(path);
        tail.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == path) return {};
        path = parent;
    }
    path = QFileInfo(path).canonicalFilePath();
    for (const auto &part : tail) path = QDir(path).filePath(part);
    return QDir::cleanPath(path);
}
bool objectFile(const QString &file, const char *schema, QJsonObject *object, QString *error) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) return fail(error, "PCV_INPUT_001: " + file);
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, "PCV_CONTRACT_001: invalid JSON: " + file);
    *object = document.object();
    if (object->value("schema_version").toString() != QLatin1String(schema))
        return fail(error, "PCV_CONTRACT_001: unsupported schema_version: " + file);
    return true;
}
bool text(const QJsonObject &o, const char *key, QString *value, QString *error) {
    if (!o.value(key).isString() || o.value(key).toString().trimmed().isEmpty())
        return fail(error, QString("PCV_CONTRACT_001: required string: %1").arg(key));
    *value = o.value(key).toString(); return true;
}
bool relative(const QString &base, const QString &root, const QString &input,
              QString *resolved, QString *error, bool mustExist = true) {
    const QString normalized = QDir::fromNativeSeparators(input);
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized) || normalized.contains(':')
        || normalized.split('/').contains(".."))
        return fail(error, "PCV_CONTRACT_001: expected contained relative path: " + input);
    *resolved = canonicalDestination(QDir(base).absoluteFilePath(normalized));
    if (resolved->isEmpty() || !inside(*resolved, root))
        return fail(error, "PCV_CONTRACT_001: path escapes task root: " + input);
    if (mustExist && !QFileInfo::exists(*resolved))
        return fail(error, "PCV_INPUT_001: missing path: " + input);
    return true;
}
bool pose(const QJsonValue &value, pointcloud::RobotPose *result, QString *error) {
    if (!value.isArray() || value.toArray().size() != 6)
        return fail(error, "PCV_TRANSFORM_002: pose requires [X,Y,Z,A,B,C]");
    const auto array = value.toArray();
    double values[6];
    for (int i = 0; i < 6; ++i) {
        if (!array.at(i).isDouble() || !std::isfinite(array.at(i).toDouble()))
            return fail(error, "PCV_TRANSFORM_002: pose requires six finite numbers");
        values[i] = array.at(i).toDouble();
    }
    *result = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return true;
}
QJsonArray poseJson(const pointcloud::RobotPose &p) { return {p.x, p.y, p.z, p.rx, p.ry, p.rz}; }
QJsonObject summary(const QString &job, const QString &status, const QString &code, const QString &message) {
    return {{"schema_version", summarySchema}, {"job_id", job}, {"status", status},
        {"error", status == "success" ? QJsonValue(QJsonValue::Null)
           : QJsonValue(QJsonObject{{"code", code}, {"message", message}})}};
}
bool outputRoot(const QString &path, QString *error) {
    const QString source = QFileInfo(QString::fromUtf8(PCV_PROCESS_SOURCE_ROOT)).canonicalFilePath();
    if (inside(canonicalDestination(path), source))
        return fail(error, "PCV_OUTPUT_001: output must be outside source tree");
    return true;
}
}

bool readProcessJob(const QString &parameterFile, PcvProcessJob *job, QString *error) {
    if (error) error->clear();
    PcvProcessJob result;
    result.parameterFile = QFileInfo(parameterFile).canonicalFilePath();
    result.taskRoot = QFileInfo(result.parameterFile).absolutePath();
    QJsonObject p, e;
    if (!objectFile(parameterFile, paramSchema, &p, error)) return false;
    auto &param = result.parameters;
    param.schemaVersion = paramSchema;
    QString executionPath, calibrationPath, outputPath;
    if (!text(p, "job_id", &param.jobId, error)
        || !text(p, "execution_result", &executionPath, error)
        || !text(p, "calibration", &calibrationPath, error)
        || !text(p, "output_dir", &outputPath, error)
        || !text(p, "mode", &param.mode, error)) return false;
    if (param.mode != "pose_only") return fail(error, "PCV_CONTRACT_001: capture process only supports pose_only");
    if (!relative(result.taskRoot, result.taskRoot, executionPath, &param.executionResult, error)
        || !relative(result.taskRoot, result.taskRoot, calibrationPath, &param.calibration, error)
        || !relative(result.taskRoot, result.taskRoot, outputPath, &param.outputDir, error, false)
        || !outputRoot(param.outputDir, error)) return false;
    if (QFileInfo::exists(param.outputDir)) return fail(error, "PCV_OUTPUT_001: output_dir already exists; choose a new directory");
    if (!objectFile(param.executionResult, executionSchema, &e, error)) return false;
    auto &execution = result.execution;
    execution.schemaVersion = executionSchema;
    QString session;
    if (!text(e, "job_id", &execution.jobId, error) || !text(e, "capture_session", &session, error)) return false;
    if (execution.jobId != param.jobId) return fail(error, "PCV_CONTRACT_001: job_id mismatch");
    if (!relative(QFileInfo(param.executionResult).absolutePath(), result.taskRoot, session,
                  &execution.captureSession, error)) return false;
    const auto records = pcv::camera::readCaptureSession(execution.captureSession, error);
    if (error && !error->isEmpty()) return false;
    const auto frames = e.value("frames").toArray();
    if (frames.size() < 2) return fail(error, "PCV_CONTRACT_001: frames requires at least two committed frames");
    QSet<quint64> ids;
    for (const auto &value : frames) {
        const auto frame = value.toObject();
        const double id = frame.value("id").toDouble(-1);
        if (!frame.value("id").isDouble() || id < 1 || id > 9007199254740991.0 || std::floor(id) != id || ids.contains(quint64(id)))
            return fail(error, "PCV_CONTRACT_001: invalid or duplicate frame id");
        PcvExecutionFrame f; f.id = quint64(id); ids.insert(f.id);
        if (!pose(frame.value("start_pose"), &f.startPose, error)
            || !pose(frame.value("end_pose"), &f.endPose, error)) return false;
        QString directory;
        qsizetype count = 0;
        for (const auto &record : records) if (record.id == f.id) {
            if (!directory.isEmpty()) return fail(error, "PCV_CONTRACT_001: duplicate capture frame id");
            directory = record.directory; count = record.pointCount;
        }
        if (directory.isEmpty()) return fail(error, QString("PCV_INPUT_002: frame id=%1 is not committed").arg(f.id));
        if (!inside(QFileInfo(directory).canonicalFilePath(), execution.captureSession)
            || !QFileInfo(QDir(directory).filePath("camera.ply")).isFile()
            || !QFileInfo(QDir(directory).filePath("raster_indices.bin")).isFile()
            || count <= 0 || count > (std::numeric_limits<qint64>::max() - 16) / 8
            || QFileInfo(QDir(directory).filePath("raster_indices.bin")).size() != 16 + count * 8)
            return fail(error, QString("PCV_INPUT_002: frame id=%1: missing or invalid PLY/raster_indices.bin").arg(f.id));
        result.request.input.frames.push_back({{}, f.startPose, f.endPose, directory});
        execution.frames.push_back(f);
    }
    pointcloud::HandEyeCalibration calibration;
    QString detail;
    if (!pointcloud::loadHandEyeCalibration(param.calibration, &calibration, &detail))
        return fail(error, "PCV_TRANSFORM_001: " + detail);
    result.request.input.calibrationPath = param.calibration;
    result.request.registration.options.enabled = false;
    result.request.registration.collectDiagnosticFrames = false;
    result.request.seam.enabled = false;
    result.request.artifacts.outputDirectory = QDir(param.outputDir).filePath("stitching");
    result.request.artifacts.allowOverwrite = false;
    *job = std::move(result); return true;
}

bool readProcessTemplate(const QString &file, PcvProcessTemplate *value, QString *error) {
    if (error) error->clear();
    QJsonObject o;
    if (!objectFile(file, templateSchema, &o, error)) return false;
    PcvProcessTemplate result;
    if (!text(o, "job_id", &result.jobId, error) || !text(o, "name", &result.name, error)) return false;
    const QString root = QFileInfo(file).absoluteDir().canonicalPath();
    for (auto field : {std::make_pair("template_info", &result.templateInfo),
                       std::make_pair("point_cloud", &result.pointCloud),
                       std::make_pair("roi_cloud", &result.roiCloud),
                       std::make_pair("plane_mask", &result.planeMask)}) {
        QString path;
        if (!text(o, field.first, &path, error) || !relative(root, root, path, field.second, error)) return false;
    }
    const auto wobj = o.value("wobj").toObject();
    if (wobj.value("num").toDouble() != 1 || wobj.value("frame").toString() != "robot_base")
        return fail(error, "PCV_FRAME_001: expected WObj1 in robot_base");
    if (!pose(wobj.value("pose"), &result.wobjPose, error)) return false;
    *value = result; return true;
}

bool readProcessSummary(const QString &file, PcvProcessSummary *value, QString *error) {
    if (error) error->clear();
    QJsonObject o;
    if (!objectFile(file, summarySchema, &o, error)) return false;
    PcvProcessSummary result;
    if (!text(o, "job_id", &result.jobId, error) || !text(o, "status", &result.status, error)) return false;
    if (result.status == "success") {
        if (!o.value("error").isNull()) return fail(error, "PCV_CONTRACT_001: success requires error=null");
    } else if (result.status == "failed" || result.status == "cancelled") {
        const auto e = o.value("error").toObject();
        if (!text(e, "code", &result.errorCode, error) || !text(e, "message", &result.message, error)) return false;
    } else return fail(error, "PCV_CONTRACT_001: unknown terminal status");
    *value = result; return true;
}

bool writeProcessSummary(const PcvProcessJob &job, const QString &status, const QString &code,
                         const QString &message, QString *error) {
    if ((status != "failed" && status != "cancelled") || code.isEmpty() || message.isEmpty())
        return fail(error, "PCV_CONTRACT_001: terminal error required; publish success with template");
    if (!outputRoot(job.parameters.outputDir, error)) return false;
    pcv::io::ArtifactTransaction tx(job.parameters.outputDir);
    if (!tx.stageJson("execution_result_summary.json", QJsonDocument(summary(job.parameters.jobId, status, code, message)).toJson())
        || !tx.commit()) return fail(error, tx.error());
    return true;
}

bool commitProcessTemplate(const PcvProcessJob &job, const PcvProcessTemplate &value,
                           const std::function<bool()> &cancelled, QString *error) {
    const QString root = canonicalDestination(job.parameters.outputDir);
    if (!outputRoot(root, error)) return false;
    if (value.jobId != job.parameters.jobId || value.name.trimmed().isEmpty())
        return fail(error, "PCV_CONTRACT_001: invalid process template job/name");
    const auto poseValue = poseJson(value.wobjPose);
    pointcloud::RobotPose checked;
    if (!pose(poseValue, &checked, error)) return false;
    QJsonObject o{{"schema_version", templateSchema}, {"job_id", value.jobId}, {"name", value.name},
        {"wobj", QJsonObject{{"num", 1}, {"frame", "robot_base"}, {"pose", poseValue}}}};
    for (auto field : {std::make_pair("template_info", value.templateInfo), std::make_pair("point_cloud", value.pointCloud),
                       std::make_pair("roi_cloud", value.roiCloud), std::make_pair("plane_mask", value.planeMask)}) {
        const QString path = QFileInfo(field.second).canonicalFilePath();
        if (!QFileInfo(path).isFile() || !inside(path, root))
            return fail(error, "PCV_OUTPUT_002: missing or external template artifact: " + field.second);
        o.insert(field.first, QDir(root).relativeFilePath(path));
    }
    if (!QFileInfo(QDir(job.request.artifacts.outputDirectory).filePath("stitched_robot_base.ply")).isFile()
        || QImage(value.planeMask).isNull())
        return fail(error, "PCV_OUTPUT_002: incomplete stitching/workpiece outputs");
    pcv::io::ArtifactTransaction tx(root, false, cancelled);
    if (!tx.stageJson("workpiece_template.json", QJsonDocument(o).toJson())
        || !tx.stageJson("execution_result_summary.json", QJsonDocument(summary(value.jobId, "success", {}, {})).toJson())
        || !tx.commit()) return fail(error, tx.error());
    return true;
}
} // namespace pcv::interface
