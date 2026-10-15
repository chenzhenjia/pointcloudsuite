#include <pcv/workpiece/workpiece_template_v1.h>
#include <pcv/infrastructure/runtime_paths.h>
#include <pcv/io/artifact_transaction.h>
#include <pcv/io/ply_reader.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pcv::workpiece {
namespace {

WorkpieceTemplateResult failure(const char *code, const QString &message, bool cancelled = false)
{
    WorkpieceTemplateResult result;
    result.errorCode = QString::fromLatin1(code);
    result.message = message;
    result.cancelled = cancelled;
    return result;
}

bool isCancelled(const std::function<bool()> &callback)
{
    return callback && callback();
}

void report(const WorkpieceTemplateProgress &callback, WorkpieceTemplateStage stage,
            qsizetype completed = 0, qsizetype total = 0)
{
    if (callback) callback(stage, completed, total);
}

bool inspectPoints(const QVector<pointcloud::Point3D> &points, WorkpieceTemplateInfo *info,
                   const std::function<bool()> &cancel, const WorkpieceTemplateProgress &progress,
                   QString *error)
{
    if (points.isEmpty()) { *error = QStringLiteral("模板点云为空"); return false; }
    QVector3D minimum, maximum;
    for (qsizetype i = 0; i < points.size(); ++i) {
        if ((i & 0x3fff) == 0) {
            if (isCancelled(cancel)) { *error = QStringLiteral("已取消"); return false; }
            report(progress, WorkpieceTemplateStage::ValidatingPoints, i, points.size());
        }
        const auto &p = points.at(i);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
            || !std::isfinite(p.nx) || !std::isfinite(p.ny) || !std::isfinite(p.nz)) {
            *error = QStringLiteral("模板点 %1 包含非有限 XYZ 或法向").arg(i);
            return false;
        }
        if (i == 0) minimum = maximum = QVector3D(p.x, p.y, p.z);
        else {
            minimum.setX(std::min(minimum.x(), p.x));
            minimum.setY(std::min(minimum.y(), p.y));
            minimum.setZ(std::min(minimum.z(), p.z));
            maximum.setX(std::max(maximum.x(), p.x));
            maximum.setY(std::max(maximum.y(), p.y));
            maximum.setZ(std::max(maximum.z(), p.z));
        }
    }
    info->pointCount = points.size();
    info->minimum = minimum;
    info->maximum = maximum;
    report(progress, WorkpieceTemplateStage::ValidatingPoints, points.size(), points.size());
    return true;
}

QJsonArray vectorJson(const QVector3D &value)
{
    return {double(value.x()), double(value.y()), double(value.z())};
}

QByteArray serialize(const WorkpieceTemplateInfo &info)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("schema_version"), QString::fromLatin1(kWorkpieceTemplateSchema)},
        {QStringLiteral("kind"), QString::fromLatin1(kWorkpieceTemplateKind)},
        {QStringLiteral("name"), info.name}, {QStringLiteral("version_id"), info.versionId},
        {QStringLiteral("created_at"), info.createdAt},
        {QStringLiteral("coordinate_frame"), info.coordinateFrame},
        {QStringLiteral("units"), info.units}, {QStringLiteral("point_count"), qint64(info.pointCount)},
        {QStringLiteral("bounds_mm"), QJsonObject{
            {QStringLiteral("minimum"), vectorJson(info.minimum)},
            {QStringLiteral("maximum"), vectorJson(info.maximum)}}},
        {QStringLiteral("point_cloud_file"), info.pointCloudFile},
        {QStringLiteral("source"), info.source}
    }).toJson(QJsonDocument::Indented);
}

bool parseVector(const QJsonValue &value, QVector3D *out)
{
    if (!value.isArray() || value.toArray().size() != 3) return false;
    const QJsonArray values = value.toArray();
    float components[3];
    for (int i = 0; i < 3; ++i) {
        if (!values.at(i).isDouble()) return false;
        const double v = values.at(i).toDouble();
        if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max()) return false;
        components[i] = float(v);
    }
    *out = QVector3D(components[0], components[1], components[2]);
    return true;
}

bool parseInfo(const QByteArray &bytes, WorkpieceTemplateInfo *info)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject object = doc.object();
    if (object.value(QStringLiteral("schema_version")).toString() != QLatin1String(kWorkpieceTemplateSchema)
        || object.value(QStringLiteral("kind")).toString() != QLatin1String(kWorkpieceTemplateKind)
        || object.value(QStringLiteral("coordinate_frame")).toString() != QStringLiteral("robot_base")
        || object.value(QStringLiteral("units")).toString() != QStringLiteral("mm")) return false;
    info->name = object.value(QStringLiteral("name")).toString();
    info->versionId = object.value(QStringLiteral("version_id")).toString();
    info->createdAt = object.value(QStringLiteral("created_at")).toString();
    info->pointCloudFile = object.value(QStringLiteral("point_cloud_file")).toString();
    const QJsonValue count = object.value(QStringLiteral("point_count"));
    const double number = count.toDouble();
    if (info->name.trimmed().isEmpty() || info->versionId.trimmed().isEmpty()
        || !QDateTime::fromString(info->createdAt, Qt::ISODateWithMs).isValid()
        || info->pointCloudFile.isEmpty() || !count.isDouble() || number < 1
        || number > std::numeric_limits<int>::max() || std::floor(number) != number
        || !object.value(QStringLiteral("source")).isObject()
        || !object.value(QStringLiteral("bounds_mm")).isObject()) return false;
    const QJsonObject bounds = object.value(QStringLiteral("bounds_mm")).toObject();
    if (!parseVector(bounds.value(QStringLiteral("minimum")), &info->minimum)
        || !parseVector(bounds.value(QStringLiteral("maximum")), &info->maximum)) return false;
    for (int i = 0; i < 3; ++i) if (info->minimum[i] > info->maximum[i]) return false;
    info->pointCount = qsizetype(number);
    info->source = object.value(QStringLiteral("source")).toObject();
    return true;
}

QString safeName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    name.replace(QStringLiteral(".."), QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    name = name.left(80);
    if (name.isEmpty()) name = QStringLiteral("workpiece");
    const QString base = name.section(QLatin1Char('.'), 0, 0);
    if (QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
            QRegularExpression::CaseInsensitiveOption).match(base).hasMatch()) name.prepend(QLatin1Char('_'));
    return name;
}

// Resolve existing ancestors too: a directory link must not bypass the source-tree guard.
QString resolvedPath(const QString &path)
{
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList suffix;
    while (!info.exists() && !info.isRoot()) {
        suffix.prepend(info.fileName());
        info = QFileInfo(info.absolutePath());
    }
    QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty()) resolved = info.absoluteFilePath();
    for (const QString &part : suffix) resolved = QDir(resolved).filePath(part);
    return QDir::fromNativeSeparators(QDir::cleanPath(resolved));
}

bool within(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return path.compare(root, sensitivity) == 0
        || path.startsWith(root + QLatin1Char('/'), sensitivity);
}

bool validTemplatePly(const QString &path, qsizetype expectedCount)
{
    // v1 owns a fixed binary layout. Check it before the generic reader, which
    // deliberately tolerates missing normals and unrelated PLY layouts.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.readLine().trimmed() != "ply"
        || file.readLine().trimmed() != "format binary_little_endian 1.0") return false;
    QStringList properties;
    bool vertex = false;
    bool ended = false;
    while (!file.atEnd() && file.pos() < 65536) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith("comment ") || line.startsWith("obj_info ") || line.isEmpty()) continue;
        if (line.startsWith("element vertex ") && !vertex) {
            bool ok = false;
            const qint64 count = line.mid(15).toLongLong(&ok);
            if (!ok || count != expectedCount) return false;
            vertex = true;
        } else if (line.startsWith("property ") && vertex) {
            properties.append(QString::fromLatin1(line));
        } else if (line == "end_header") { ended = true; break; }
        else return false;
    }
    const QStringList fields = {QStringLiteral("property float x"), QStringLiteral("property float y"),
        QStringLiteral("property float z"), QStringLiteral("property float nx"),
        QStringLiteral("property float ny"), QStringLiteral("property float nz")};
    return vertex && ended && properties == fields
        && file.size() - file.pos() == qint64(expectedCount) * 6 * sizeof(float);
}

} // namespace

QString defaultWorkpieceTemplateRoot()
{
    return QDir(pcv::runtime::dataDirectory()).filePath(QStringLiteral("workpiece_templates"));
}

WorkpieceTemplateResult saveWorkpieceTemplate(const SaveWorkpieceTemplateRequest &request)
{
    if (isCancelled(request.isCancelled)) return failure("PCV_CONTRACT_001", QStringLiteral("已取消"), true);
    if (request.name.trimmed().isEmpty() || request.coordinateFrame != QStringLiteral("robot_base"))
        return failure("PCV_CONTRACT_001", QStringLiteral("需要工件名称及已确认的 robot_base 点云"));
    WorkpieceTemplateInfo info;
    info.name = request.name;
    info.source = request.source;
    QString error;
    if (!inspectPoints(request.points, &info, request.isCancelled, request.progress, &error))
        return failure("PCV_INPUT_002", error, error == QStringLiteral("已取消"));
    const QString rootPath = request.outputRoot.isEmpty() ? defaultWorkpieceTemplateRoot() : request.outputRoot;
    const QString resolvedRoot = resolvedPath(rootPath);
    if (within(resolvedRoot, resolvedPath(QString::fromUtf8(PCV_WORKPIECE_TEMPLATE_SOURCE_DIR))))
        return failure("PCV_OUTPUT_001", QStringLiteral("模板输出必须位于源码树外"));
    QDir root(QFileInfo(rootPath).absoluteFilePath());
    if (!QDir().mkpath(root.absolutePath()))
        return failure("PCV_OUTPUT_001", QStringLiteral("无法创建模板根目录"));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    info.createdAt = now.toString(Qt::ISODateWithMs);
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString base = safeName(request.name) + QStringLiteral("_v1_") + stamp;
    QString directoryName;
    for (int suffix = 0; suffix < 10000; ++suffix) {
        if (isCancelled(request.isCancelled)) return failure("PCV_OUTPUT_002", QStringLiteral("已取消"), true);
        directoryName = base + (suffix ? QStringLiteral("_%1").arg(suffix) : QString());
        if (QFileInfo::exists(root.filePath(directoryName))) continue;
        if (root.mkdir(directoryName)) {
            info.versionId = stamp + (suffix ? QStringLiteral("_%1").arg(suffix) : QString());
            break;
        }
        if (!QFileInfo::exists(root.filePath(directoryName)))
            return failure("PCV_OUTPUT_001", QStringLiteral("无法创建模板版本目录"));
    }
    if (info.versionId.isEmpty()) return failure("PCV_OUTPUT_001", QStringLiteral("模板版本目录冲突过多"));
    const QString output = root.filePath(directoryName);
    pcv::io::ArtifactTransaction transaction(output, false, request.isCancelled);
    report(request.progress, WorkpieceTemplateStage::WritingCloud);
    bool ok = transaction.stageBinaryPly(QString::fromLatin1(kWorkpieceTemplatePly), request.points, true);
    if (ok) {
        report(request.progress, WorkpieceTemplateStage::WritingMetadata);
        ok = transaction.stageJson(QString::fromLatin1(kWorkpieceTemplateJson), serialize(info));
    }
    if (ok) {
        report(request.progress, WorkpieceTemplateStage::Committing);
        ok = transaction.commit();
    }
    if (!ok) {
        const QString message = transaction.error();
        transaction.abort();
        root.rmdir(directoryName); // Only our newly reserved, empty version directory.
        return failure("PCV_OUTPUT_002", message, message == QStringLiteral("已取消"));
    }
    // Once committed, a late cancellation cannot undo/report failure of the saved package.
    WorkpieceTemplateResult result;
    result.success = true;
    result.info = info;
    result.outputDirectory = output;
    result.infoFile = QDir(output).filePath(QString::fromLatin1(kWorkpieceTemplateJson));
    result.pointCloudFile = QDir(output).filePath(QString::fromLatin1(kWorkpieceTemplatePly));
    return result;
}

WorkpieceTemplateResult loadWorkpieceTemplate(const LoadWorkpieceTemplateRequest &request)
{
    if (isCancelled(request.isCancelled)) return failure("PCV_INPUT_002", QStringLiteral("已取消"), true);
    report(request.progress, WorkpieceTemplateStage::ReadingMetadata);
    QFile file(request.infoFile);
    if (!file.open(QIODevice::ReadOnly)) return failure("PCV_INPUT_001", file.errorString());
    WorkpieceTemplateInfo info;
    if (!parseInfo(file.readAll(), &info))
        return failure("PCV_CONTRACT_001", QStringLiteral("模板 JSON 契约无效或版本不支持"));
    const QString root = QFileInfo(request.infoFile).absolutePath();
    const QString relative = QDir::fromNativeSeparators(info.pointCloudFile);
    if (QFileInfo(relative).isAbsolute() || relative.contains(QLatin1Char(':'))
        || relative.split(QLatin1Char('/')).contains(QStringLiteral("..")))
        return failure("PCV_CONTRACT_001", QStringLiteral("模板 PLY 必须使用包内相对路径"));
    const QString ply = QDir(root).filePath(relative);
    if (!within(resolvedPath(ply), resolvedPath(root)))
        return failure("PCV_CONTRACT_001", QStringLiteral("模板 PLY 路径越出模板目录"));
    if (!validTemplatePly(ply, info.pointCount))
        return failure("PCV_INPUT_002", QStringLiteral("模板 PLY 缺失、截断、点数或 XYZ/法向布局不符"));
    report(request.progress, WorkpieceTemplateStage::ReadingCloud);
    pcv::detail::io::PlyReadOptions options;
    options.isCancelled = request.isCancelled;
    options.progress = [progress = request.progress](qsizetype completed, qsizetype total) {
        report(progress, WorkpieceTemplateStage::ReadingCloud, completed, total);
    };
    auto cloud = pcv::detail::io::readPly(ply, options);
    if (!cloud.ok) return failure("PCV_INPUT_002", cloud.error, cloud.cancelled);
    if (cloud.declaredPointCount != info.pointCount || cloud.points.size() != info.pointCount)
        return failure("PCV_CONTRACT_001", QStringLiteral("模板点数不一致"));
    WorkpieceTemplateInfo measured;
    QString error;
    if (!inspectPoints(cloud.points, &measured, request.isCancelled, request.progress, &error))
        return failure("PCV_INPUT_002", error, error == QStringLiteral("已取消"));
    if (measured.minimum != info.minimum || measured.maximum != info.maximum)
        return failure("PCV_CONTRACT_001", QStringLiteral("模板包围盒与点云不一致"));
    if (isCancelled(request.isCancelled)) return failure("PCV_INPUT_002", QStringLiteral("已取消"), true);
    WorkpieceTemplateResult result;
    result.success = true;
    result.info = info;
    result.infoFile = QFileInfo(request.infoFile).absoluteFilePath();
    result.outputDirectory = root;
    result.pointCloudFile = ply;
    result.points = std::move(cloud.points);
    return result;
}

} // namespace pcv::workpiece
