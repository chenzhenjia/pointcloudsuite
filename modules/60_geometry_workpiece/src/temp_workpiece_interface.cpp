#include <pcv/workpiece/temp_workpiece_interface.h>

#include <pcv/workpiece/roi_matching.h>
#include <pcv/io/artifact_transaction.h>

#include <pcv/infrastructure/runtime_paths.h>
#include <pcv/io/cloud_cache.h>
#include <pcv/io/ply_reader.h>
#include <pcv/io/ply_writer.h>

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace pcv::interface {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

TempWorkpiecePreparation failedPreparation(const QString &code, const QString &message,
                                           QString *error)
{
    if (error) *error = QStringLiteral("%1: %2").arg(code, message);
    TempWorkpiecePreparation result;
    result.errorCode = code;
    result.message = message;
    return result;
}

TempWorkpieceResult failedResult(const QString &code, const QString &message, QString *error)
{
    if (error) *error = QStringLiteral("%1: %2").arg(code, message);
    TempWorkpieceResult result;
    result.errorCode = code;
    result.message = message;
    return result;
}

bool invalidComponent(const QString &value)
{
    const QString text = value.trimmed();
    for (const QChar character : QStringLiteral("<>:\"|?*")) {
        if (text.contains(character)) return true;
    }
    return text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")
        || text.contains(QStringLiteral("..")) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || QFileInfo(text).isAbsolute()
        || text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(' '));
}

QString interfaceDirectory(const QString &runtimeRoot, const QString &jobId)
{
    return QDir(runtimeRoot).filePath(QStringLiteral("jobs/%1/interface").arg(jobId));
}

bool resolveRelativeToDirectory(const QString &directory, const QString &input,
                                bool restrictToDirectory, QString *resolved, QString *error)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) return fail(error, QStringLiteral("file path is empty"));
    const QFileInfo fileInfo(text);
    if (fileInfo.isAbsolute()) {
        if (resolved) *resolved = QDir::cleanPath(fileInfo.absoluteFilePath());
        return true;
    }
    const QString cleanDirectory = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    const QString cleanPath = QDir::cleanPath(
        QFileInfo(QDir(directory).filePath(text)).absoluteFilePath());
    const bool insideDirectory = cleanPath.startsWith(cleanDirectory + QLatin1Char('/'),
                                                       Qt::CaseInsensitive)
        || cleanPath.compare(cleanDirectory, Qt::CaseInsensitive) == 0;
    if (restrictToDirectory && !insideDirectory)
        return fail(error, QStringLiteral("path escapes JSON directory: %1").arg(input));
    if (resolved) *resolved = cleanPath;
    return true;
}

bool parsePose(const QJsonValue &value, pointcloud::RobotPose *pose, QString *error)
{
    QJsonArray array;
    if (value.isArray()) array = value.toArray();
    else if (value.isObject()) {
        const QStringList keys{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                               QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
        for (const QString &key : keys) array.push_back(value.toObject().value(key));
    } else return fail(error, QStringLiteral("pose must be an array or object"));
    if (array.size() != 6) return fail(error, QStringLiteral("pose array must contain 6 numbers"));
    double values[6] = {};
    for (int i = 0; i < 6; ++i) {
        if (!array.at(i).isDouble()) return fail(error, QStringLiteral("pose contains non-number"));
        values[i] = array.at(i).toDouble();
        if (!std::isfinite(values[i])) return fail(error, QStringLiteral("pose contains non-finite number"));
    }
    pose->x = values[0]; pose->y = values[1]; pose->z = values[2];
    // Controller order is [X,Y,Z,A,B,C], where A=Rx, B=Ry, C=Rz.
    pose->rx = values[3]; pose->ry = values[4]; pose->rz = values[5];
    return true;
}

bool parseSeedIndices(const QJsonValue &value, QVector<int> *indices, QString *error)
{
    if (!value.isArray() || value.toArray().size() != 3)
        return fail(error, QStringLiteral("plane_seed_indices must contain exactly 3 indices"));
    QVector<int> parsed;
    for (const QJsonValue item : value.toArray()) {
        const double number = item.toDouble(-1.0);
        if (!item.isDouble() || !std::isfinite(number) || number < 0.0
            || number > double(std::numeric_limits<int>::max()) || std::floor(number) != number)
            return fail(error, QStringLiteral("plane_seed_indices contains invalid index"));
        parsed.push_back(int(number));
    }
    *indices = parsed;
    return true;
}

bool parseLayout(const QJsonObject &scan, TempScanningInfo *info, QString *error)
{
    const QJsonValue value = scan.value(QStringLiteral("point_cloud_layout"));
    if (value.isUndefined()) {
        info->pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
        info->layoutWarning = QStringLiteral("point_cloud_layout missing; defaulted to LineProfileXz");
        return true;
    }
    if (!value.isString()) return fail(error, QStringLiteral("point_cloud_layout must be a string"));
    const QString text = value.toString();
    if (text.compare(QStringLiteral("LineProfileXz"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("line_profile_xz"), Qt::CaseInsensitive) == 0) {
        info->pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
        return true;
    }
    return fail(error, QStringLiteral("point_cloud_layout must be LineProfileXz; got: %1").arg(text));
}

QString inputErrorCode(const QString &detail)
{
    if (detail.contains(QStringLiteral("pose"), Qt::CaseInsensitive))
        return QString::fromLatin1(kErrorPoseInvalid);
    if (detail.startsWith(QStringLiteral("invalid JSON"))
        || detail.startsWith(QStringLiteral("failed to open temp_scanning_info.json"))
        || detail.startsWith(QStringLiteral("unsupported point_cloud_layout"))
        || detail.startsWith(QStringLiteral("point_cloud_layout must be"))
        || detail.startsWith(QStringLiteral("created_at")))
        return QString::fromLatin1(kErrorInputUnsupported);
    return QString::fromLatin1(kErrorContract);
}

QString jsonPath(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

QString quotedJsonMetadata(const QString &value)
{
    const QString array = QString::fromUtf8(
        QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
    return array.mid(1, array.size() - 2);
}

QString jsonNumberMetadata(double value)
{
    return QString::number(value, 'g', 15);
}

QString jsonNumberFixed3Metadata(double value)
{
    return QString::number(value, 'f', 3);
}

QString workpieceEquationJson(const TempWorkpieceFinalizeOptions &options)
{
    // Formal output order is X,Y,Z,A,B,C mapped from the frame's stored
    // pose fields as C,B,A for the controller contract.
    return QStringLiteral("[\n"
                          "      %1,\n"
                          "      %2,\n"
                          "      %3,\n"
                          "      %4,\n"
                          "      %5,\n"
                          "      %6\n"
                          "    ]")
        .arg(jsonNumberFixed3Metadata(options.originInRobotBase.x()),
             jsonNumberFixed3Metadata(options.originInRobotBase.y()),
             jsonNumberFixed3Metadata(options.originInRobotBase.z()),
             jsonNumberFixed3Metadata(options.abcDeg.z()),
             jsonNumberFixed3Metadata(options.abcDeg.y()),
             jsonNumberFixed3Metadata(options.abcDeg.x()));
}

bool validIndices(const QVector<int> &indices, qsizetype pointCount, QString *error)
{
    if (indices.isEmpty()) return fail(error, QStringLiteral("point index list is empty"));
    for (const int index : indices) {
        if (index < 0 || index >= pointCount)
            return fail(error, QStringLiteral("point index is outside robot_base cloud"));
    }
    return true;
}

QByteArray metadataJson(const TempWorkpieceFinalizeOptions &options,
                        const QString &createdAt,
                        const QString &baseline, const QString &roiPath, const QString &mask)
{
    const QString baselinePath = jsonPath(baseline);
    const QString roiPathValue = jsonPath(roiPath);
    const QString maskPath = jsonPath(mask);
    QString text;
    QTextStream stream(&text);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "{\n"
           << "  \"schema_version\": " << quotedJsonMetadata(QString::fromLatin1(kTempWorkpieceSchema)) << ",\n"
           << "  \"kind\": " << quotedJsonMetadata(QString::fromLatin1(kTempWorkpieceKind)) << ",\n"
           << "  \"created_at\": " << quotedJsonMetadata(createdAt) << ",\n"
           << "  \"plane\": {\n"
           << "    \"equation\": " << workpieceEquationJson(options) << ",\n"
           << "    \"wobj_num\": 1\n"
           << "  },\n"
           << "  \"image\": {\n"
           << "    \"name\": \"plane_mask.png\",\n"
           << "    \"width_px\": " << options.planeMask.width() << ",\n"
           << "    \"height_px\": " << options.planeMask.height() << ",\n"
           << "    \"width_mm\": " << jsonNumberMetadata(options.planeMask.width() * 0.05) << ",\n"
           << "    \"height_mm\": " << jsonNumberMetadata(options.planeMask.height() * 0.05) << ",\n"
           << "    \"pixel_size_mm\": " << jsonNumberMetadata(0.05) << "\n"
           << "  },\n"
           << "  \"roi\": \"rectangle\",\n"
           << "  \"outputs\": {\n"
           << "    \"robot_base_point_cloud\": " << quotedJsonMetadata(baselinePath) << ",\n"
           << "    \"roi_point_cloud\": " << quotedJsonMetadata(roiPathValue) << ",\n"
           << "    \"plane_mask\": " << quotedJsonMetadata(maskPath) << "\n"
           << "  }\n"
           << "}\n";
    stream.flush();
    return text.toUtf8();
}

bool resolveAbsoluteFile(const QString &input, QString *resolved, QString *error)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) return fail(error, QStringLiteral("file path is empty"));
    const QFileInfo fileInfo(text);
    if (!fileInfo.isAbsolute())
        return fail(error, QStringLiteral("path must be absolute: %1").arg(input));
    if (resolved) *resolved = QDir::cleanPath(fileInfo.absoluteFilePath());
    return true;
}

// Plain-PLY line: the UI builds the scan descriptor in memory because a bare
// PLY carries no scanning JSON metadata. The rules mirror the JSON contract so
// both entries reject the same malformed descriptions.
bool validateInMemoryScan(const TempScanningInfo &scan, QString *error)
{
    if (scan.scanId.trimmed().isEmpty())
        return fail(error, QStringLiteral("scan_id is required"));
    if (scan.createdAtIso8601.trimmed().isEmpty()
        || !QDateTime::fromString(scan.createdAtIso8601, Qt::ISODate).isValid())
        return fail(error, QStringLiteral("created_at is not a valid ISO 8601 timestamp"));
    if (scan.coordinateFrame != QStringLiteral("camera"))
        return fail(error, QStringLiteral("scan coordinate_frame must be camera"));
    if (scan.calibrationSourceFrame != QStringLiteral("camera")
        || scan.calibrationTargetFrame != QStringLiteral("robot_base"))
        return fail(error, QStringLiteral("calibration frames must be camera -> robot_base"));
    if (scan.pointCloudLayout != pointcloud::DepthPointLayout::LineProfileXz)
        return fail(error, QStringLiteral(
            "unsupported point_cloud_layout: only LineProfileXz is supported"));
    if (scan.pointCloudFile.trimmed().isEmpty())
        return fail(error, QStringLiteral("point_cloud_file is required"));
    if (scan.calibrationFile.trimmed().isEmpty())
        return fail(error, QStringLiteral("calibration_file is required"));
    const pointcloud::RobotPose *poses[2] = {&scan.robotPoseStart, &scan.robotPoseEnd};
    const char *const poseNames[2] = {"robot_pose_start", "robot_pose_end"};
    for (int poseIndex = 0; poseIndex < 2; ++poseIndex) {
        const double values[6] = {poses[poseIndex]->x, poses[poseIndex]->y, poses[poseIndex]->z,
                                  poses[poseIndex]->rx, poses[poseIndex]->ry, poses[poseIndex]->rz};
        for (const double value : values) {
            if (!std::isfinite(value))
                return fail(error, QStringLiteral("%1 contains non-finite number")
                                       .arg(QString::fromLatin1(poseNames[poseIndex])));
        }
    }
    return true;
}

// Shared conversion tail for both entries: cached PLY read, hand-eye load and
// the single coordinate-conversion implementation from module 50. The caller
// supplies the already-resolved file paths.
TempWorkpiecePreparation transformScanToRobotBase(
    const TempScanningInfo &scan,
    const QString &resolvedPly,
    const QString &resolvedCalibration,
    const std::function<bool()> &isCancelled,
    QString *error)
{
    pcv::detail::io::PlyReadOptions readOptions;
    readOptions.isCancelled = isCancelled;
    const auto cached = pcv::detail::io::readPlyCached(resolvedPly, {}, readOptions);
    if (!cached.ok) {
        TempWorkpiecePreparation failure = failedPreparation(
            QString::fromLatin1(kErrorInputUnsupported), cached.error, error);
        failure.cancelled = cached.cancelled;
        return failure;
    }
    pointcloud::HandEyeCalibration calibration;
    QString detail;
    if (!pointcloud::loadHandEyeCalibration(resolvedCalibration, &calibration, &detail))
        return failedPreparation(QString::fromLatin1(kErrorTransformInvalid), detail, error);
    pointcloud::CloudTransformOptions transformOptions;
    transformOptions.layout = scan.pointCloudLayout;
    transformOptions.sampleStride = 1;
    transformOptions.interpolateRotation = true;
    transformOptions.isCancelled = isCancelled;
    pointcloud::RobotCloudResult cloud = pointcloud::transformLineScanToRobotBase(
        cached.points, calibration, scan.robotPoseStart, scan.robotPoseEnd, transformOptions);
    if (!cloud.ok) {
        TempWorkpiecePreparation failure = failedPreparation(
            QString::fromLatin1(kErrorTransformInvalid), cloud.error, error);
        failure.declaredPointCount = cached.declaredPointCount;
        failure.cancelled = cloud.cancelled;
        return failure;
    }
    TempWorkpiecePreparation result;
    result.success = true;
    result.declaredPointCount = cached.declaredPointCount;
    result.convertedPointCount = cloud.points.size();
    result.rejectedInvalidPointCount = cloud.rejectedInvalid;
    result.rejectedRangePointCount = cloud.rejectedRange;
    result.inputYMinimum = cloud.inputYMinimum;
    result.inputYMaximum = cloud.inputYMaximum;
    result.signedTravel = cloud.signedTravel;
    result.dominantTravelAxis = cloud.dominantTravelAxis;
    result.robotBasePoints = std::move(cloud.points);
    result.sourceIndices = std::move(cloud.sourceIndices);
    return result;
}

// Plain-PLY preparation: no runtime job directory and no scanning JSON, the
// output directory comes from the user-selected absolute path.
TempWorkpiecePreparation prepareInMemoryTempWorkpiece(const TempWorkpieceOptions &options,
                                                      const QString &runtimeRoot,
                                                      QString *error)
{
    QString detail;
    if (!validateInMemoryScan(options.scanInfo, &detail))
        return failedPreparation(inputErrorCode(detail), detail, error);
    if (options.outputDirectory.trimmed().isEmpty())
        return failedPreparation(QString::fromLatin1(kErrorContract),
                                 QStringLiteral("outputDirectory is required"), error);
    const QFileInfo outputInfo(options.outputDirectory);
    if (!outputInfo.isAbsolute())
        return failedPreparation(QString::fromLatin1(kErrorContract),
                                 QStringLiteral("outputDirectory must be absolute"), error);
    const QString outputDirectory = QDir::cleanPath(outputInfo.absoluteFilePath());
    QString resolvedPly;
    if (!resolveAbsoluteFile(options.scanInfo.pointCloudFile, &resolvedPly, &detail))
        return failedPreparation(QString::fromLatin1(kErrorContract), detail, error);
    if (!QFileInfo::exists(resolvedPly))
        return failedPreparation(QString::fromLatin1(kErrorInputMissing),
                                 QStringLiteral("point cloud file is missing"), error);
    const QString calibrationInput = options.calibrationOverridePath.trimmed().isEmpty()
        ? options.scanInfo.calibrationFile : options.calibrationOverridePath;
    QString resolvedCalibration;
    if (!resolveAbsoluteFile(calibrationInput, &resolvedCalibration, &detail))
        return failedPreparation(QString::fromLatin1(kErrorContract), detail, error);
    if (!QFileInfo::exists(resolvedCalibration))
        return failedPreparation(QString::fromLatin1(kErrorInputMissing),
                                 QStringLiteral("calibration file is missing"), error);
    TempWorkpiecePreparation result = transformScanToRobotBase(
        options.scanInfo, resolvedPly, resolvedCalibration, options.isCancelled, error);
    if (!result.success) return result;
    result.success = true;
    result.runtimeRoot = runtimeRoot;
    result.jobId = options.jobId;
    result.interfaceDirectory = outputDirectory;
    // The plain-PLY line never writes a temp_scanning_info.json artifact.
    result.scanningInfoPath.clear();
    result.outputDirectory = outputDirectory;
    result.scanId = options.scanInfo.scanId;
    result.schemaVersion = options.scanInfo.schemaVersion;
    result.kind = options.scanInfo.kind;
    result.sourceFrame = options.scanInfo.coordinateFrame;
    result.targetFrame = options.scanInfo.calibrationTargetFrame;
    result.coordinateFrame = QStringLiteral("robot_base");
    result.createdAtIso8601 = options.scanInfo.createdAtIso8601;
    result.calibrationSourceFrame = options.scanInfo.calibrationSourceFrame;
    result.calibrationTargetFrame = options.scanInfo.calibrationTargetFrame;
    result.robotPoseStart = options.scanInfo.robotPoseStart;
    result.robotPoseEnd = options.scanInfo.robotPoseEnd;
    result.warning = options.scanInfo.warning;
    result.resolvedPointCloudFile = resolvedPly;
    result.resolvedCalibrationFile = resolvedCalibration;
    result.pointCloudLayout = options.scanInfo.pointCloudLayout;
    return result;
}

} // namespace

QString defaultRuntimeRoot()
{
    return QDir(pcv::runtime::applicationDataDirectory()).filePath(QStringLiteral("runtime_data"));
}

bool parseTempScanningInfo(const QString &filePath, TempScanningInfo *info, QString *error)
{
    if (!info) return fail(error, QStringLiteral("TempScanningInfo output is null"));
    *info = {};
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("failed to open temp_scanning_info.json: %1").arg(filePath));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toString() != QString::fromLatin1(kTempScanningSchema))
        return fail(error, QStringLiteral("unsupported schema_version"));
    if (root.value(QStringLiteral("kind")).toString() != QString::fromLatin1(kTempScanningKind))
        return fail(error, QStringLiteral("unsupported kind"));
    const QJsonObject scan = root.value(QStringLiteral("scan")).toObject();
    const QJsonObject calibration = root.value(QStringLiteral("calibration")).toObject();
    if (scan.isEmpty() || calibration.isEmpty())
        return fail(error, QStringLiteral("scan/calibration object is required"));
    info->schemaVersion = root.value(QStringLiteral("schema_version")).toString();
    info->kind = root.value(QStringLiteral("kind")).toString();
    const QJsonValue createdAt = root.value(QStringLiteral("created_at"));
    if (createdAt.isUndefined() || !createdAt.isString()
        || createdAt.toString().trimmed().isEmpty()
        || !QDateTime::fromString(createdAt.toString(), Qt::ISODate).isValid())
        return fail(error, QStringLiteral("created_at is not a valid ISO 8601 timestamp"));
    info->createdAtIso8601 = createdAt.toString().trimmed();
    info->scanId = scan.value(QStringLiteral("scan_id")).toString();
    info->pointCloudFile = scan.value(QStringLiteral("point_cloud_file")).toString();
    info->coordinateFrame = scan.value(QStringLiteral("coordinate_frame")).toString();
    info->calibrationFile = calibration.value(QStringLiteral("calibration_file")).toString();
    info->calibrationSourceFrame = calibration.value(QStringLiteral("source_frame")).toString();
    info->calibrationTargetFrame = calibration.value(QStringLiteral("target_frame")).toString();
    if (info->scanId.trimmed().isEmpty() || info->pointCloudFile.trimmed().isEmpty()
        || info->calibrationFile.trimmed().isEmpty())
        return fail(error, QStringLiteral("scan_id, point_cloud_file and calibration_file are required"));
    if (info->coordinateFrame != QStringLiteral("camera"))
        return fail(error, QStringLiteral("scan coordinate_frame must be camera"));
    if (info->calibrationSourceFrame != QStringLiteral("camera")
        || info->calibrationTargetFrame != QStringLiteral("robot_base"))
        return fail(error, QStringLiteral("calibration frames must be camera -> robot_base"));
    if (!parseLayout(scan, info, error)) return false;
    if (!scan.contains(QStringLiteral("robot_pose_start"))
        || !parsePose(scan.value(QStringLiteral("robot_pose_start")), &info->robotPoseStart, error))
        return false;
    if (!scan.contains(QStringLiteral("robot_pose_end"))
        || !parsePose(scan.value(QStringLiteral("robot_pose_end")), &info->robotPoseEnd, error))
        return false;
    if (scan.contains(QStringLiteral("plane_seed_indices"))
        && !parseSeedIndices(scan.value(QStringLiteral("plane_seed_indices")),
                             &info->planeSeedIndices, error)) return false;
    const QString directory = QFileInfo(filePath).absolutePath();
    QString resolved;
    if (!resolveRelativeToDirectory(directory, info->pointCloudFile, true, &resolved, error)
        || !resolveRelativeToDirectory(directory, info->calibrationFile, true, &resolved, error))
        return false;
    if (!info->layoutWarning.isEmpty()) {
        if (!info->warning.isEmpty()) info->warning += QStringLiteral("; ");
        info->warning += info->layoutWarning;
    }
    info->valid = true;
    return true;
}

TempWorkpiecePreparation prepareTempWorkpiece(const TempWorkpieceOptions &options, QString *error)
{
    if (options.isCancelled && options.isCancelled()) {
        TempWorkpiecePreparation cancelled =
            failedPreparation(QStringLiteral("PCV_CANCELLED"), QStringLiteral("cancelled"), error);
        cancelled.cancelled = true;
        return cancelled;
    }
    // The plain-PLY line has no scanning JSON and no runtime job directory: the
    // descriptor and the absolute output directory come from the parameter
    // dialog. The JSON entry keeps the original behaviour below.
    if (options.useInMemoryScan) {
        return prepareInMemoryTempWorkpiece(
            options,
            options.runtimeRoot.trimmed().isEmpty()
                ? defaultRuntimeRoot() : QDir::cleanPath(options.runtimeRoot),
            error);
    }
    const bool explicitPath = !options.scanningInfoPath.trimmed().isEmpty();
    const QString runtimeRoot = options.runtimeRoot.trimmed().isEmpty()
        ? defaultRuntimeRoot() : QDir::cleanPath(options.runtimeRoot);
    if (runtimeRoot.trimmed().isEmpty()
        || (!explicitPath && invalidComponent(options.jobId))
        || (!options.jobId.trimmed().isEmpty() && invalidComponent(options.jobId)))
        return failedPreparation(QString::fromLatin1(kErrorContract),
                                 QStringLiteral("runtimeRoot/jobId is invalid"), error);
    QString scanningInfoPath = options.scanningInfoPath.trimmed();
    QString jsonDirectory;
    if (scanningInfoPath.isEmpty()) {
        jsonDirectory = interfaceDirectory(runtimeRoot, options.jobId);
        scanningInfoPath = QDir(jsonDirectory).filePath(QStringLiteral("temp_scanning_info.json"));
    } else {
        scanningInfoPath = QDir::cleanPath(QFileInfo(scanningInfoPath).absoluteFilePath());
        jsonDirectory = QFileInfo(scanningInfoPath).absolutePath();
    }
    if (!QFileInfo::exists(scanningInfoPath))
        return failedPreparation(QString::fromLatin1(kErrorInputMissing),
                                 QStringLiteral("temp_scanning_info.json is missing"), error);
    QString detail;
    TempScanningInfo scan;
    if (!parseTempScanningInfo(scanningInfoPath, &scan, &detail))
        return failedPreparation(inputErrorCode(detail), detail, error);
    QString outputDirectory = options.outputDirectory.trimmed();
    if (outputDirectory.isEmpty()) outputDirectory = jsonDirectory;
    else {
        QString resolvedOutput;
        if (!resolveRelativeToDirectory(jsonDirectory, outputDirectory, true,
                                         &resolvedOutput, &detail))
            return failedPreparation(QString::fromLatin1(kErrorContract), detail, error);
        outputDirectory = resolvedOutput;
    }
    QString resolvedPly;
    if (!resolveRelativeToDirectory(jsonDirectory, scan.pointCloudFile, true,
                                    &resolvedPly, &detail))
        return failedPreparation(QString::fromLatin1(kErrorContract), detail, error);
    if (!QFileInfo::exists(resolvedPly))
        return failedPreparation(QString::fromLatin1(kErrorInputMissing),
                                 QStringLiteral("point cloud file is missing"), error);
    QString resolvedCalibration;
    const QString calibrationInput = options.calibrationOverridePath.trimmed().isEmpty()
        ? scan.calibrationFile : options.calibrationOverridePath;
    const bool restrictCalibrationToJsonDirectory = options.calibrationOverridePath.trimmed().isEmpty();
    if (!resolveRelativeToDirectory(jsonDirectory, calibrationInput,
                                    restrictCalibrationToJsonDirectory,
                                    &resolvedCalibration, &detail))
        return failedPreparation(QString::fromLatin1(kErrorContract), detail, error);
    if (!QFileInfo::exists(resolvedCalibration))
        return failedPreparation(QString::fromLatin1(kErrorInputMissing),
                                 QStringLiteral("calibration file is missing"), error);
    TempWorkpiecePreparation result = transformScanToRobotBase(
        scan, resolvedPly, resolvedCalibration, options.isCancelled, error);
    if (!result.success) return result;
    result.success = true;
    result.runtimeRoot = runtimeRoot; result.jobId = options.jobId;
    result.interfaceDirectory = jsonDirectory; result.scanningInfoPath = scanningInfoPath;
    result.outputDirectory = outputDirectory; result.scanId = scan.scanId;
    result.schemaVersion = scan.schemaVersion; result.kind = scan.kind;
    result.sourceFrame = scan.coordinateFrame; result.targetFrame = scan.calibrationTargetFrame;
    result.coordinateFrame = QStringLiteral("robot_base");
    result.createdAtIso8601 = scan.createdAtIso8601;
    result.calibrationSourceFrame = scan.calibrationSourceFrame;
    result.calibrationTargetFrame = scan.calibrationTargetFrame;
    result.robotPoseStart = scan.robotPoseStart; result.robotPoseEnd = scan.robotPoseEnd;
    result.warning = scan.warning; result.resolvedPointCloudFile = resolvedPly;
    result.resolvedCalibrationFile = resolvedCalibration;
    result.pointCloudLayout = scan.pointCloudLayout;
    return result;
}

TempWorkpieceResult finalizeTempWorkpiece(const TempWorkpiecePreparation &prepared,
                                          const TempWorkpieceFinalizeOptions &options,
                                          QString *error)
{
    if (options.isCancelled && options.isCancelled())
        return failedResult(QStringLiteral("PCV_CANCELLED"), QStringLiteral("cancelled"), error);
    if (!prepared.success || prepared.robotBasePoints.isEmpty()
        || prepared.sourceIndices.size() != prepared.robotBasePoints.size())
        return failedResult(QString::fromLatin1(kErrorContract),
                            QStringLiteral("prepared robot_base cloud is invalid"), error);
    if (options.outputDirectory.trimmed().isEmpty())
        return failedResult(QString::fromLatin1(kErrorContract), QStringLiteral("outputDirectory is required"), error);
    if (options.planeMask.isNull() || options.planeMask.format() != QImage::Format_Grayscale8
        || options.planeMask.width() <= 0 || options.planeMask.height() <= 0)
        return failedResult(QString::fromLatin1(kErrorImage),
                            QStringLiteral("plane mask image must be non-empty Grayscale8"), error);
    QString detail;
    const QVector3D modelNormal(options.planeModel.x(), options.planeModel.y(), options.planeModel.z());
    if (!std::isfinite(modelNormal.x()) || !std::isfinite(modelNormal.y())
        || !std::isfinite(modelNormal.z()) || modelNormal.lengthSquared() <= 1.0e-10f)
        return failedResult(QString::fromLatin1(kErrorPlane), QStringLiteral("plane model is invalid"), error);

    // The preparation stage already converted the complete cloud to
    // robot_base. Reuse the module-40 matching contract with an identity
    // transform here so ROI validation and source-index preservation follow
    // the same fail-closed path as camera-coordinate ROI matching.
    pointcloud::HandEyeCalibration identityCalibration;
    identityCalibration.flangeFromDepth.setToIdentity();
    identityCalibration.valid = true;
    QMatrix4x4 identity;
    identity.setToIdentity();
    pointcloud::CloudTransformOptions identityOptions;
    identityOptions.layout = pointcloud::DepthPointLayout::FullXyz;
    identityOptions.sampleStride = 1;
    identityOptions.interpolateRotation = false;
    pcv::matching::RoiMatchInput matchInput;
    matchInput.sourcePoints = prepared.robotBasePoints;
    matchInput.planeIndices = options.planeIndices;
    matchInput.roiIndices = options.roiIndices;
    matchInput.planeModel = options.planeModel;
    matchInput.calibration = identityCalibration;
    matchInput.startBaseFromFlange = identity;
    matchInput.endBaseFromFlange = identity;
    matchInput.transformOptions = identityOptions;
    matchInput.isCancelled = options.isCancelled;
    const pcv::matching::RoiMatchResult matched =
        pcv::matching::matchRoiToRobotBase(matchInput);
    if (matched.cancelled)
        return failedResult(QStringLiteral("PCV_CANCELLED"), QStringLiteral("cancelled"), error);
    if (!matched.ok)
        return failedResult(QString::fromLatin1(kErrorPlane), matched.error, error);
    bool invertible = false;
    options.TBaseWorkpiece.inverted(&invertible);
    if (!invertible)
        return failedResult(QString::fromLatin1(kErrorFrame), QStringLiteral("T_base_workpiece is not invertible"), error);
    if (!QDir().mkpath(options.outputDirectory))
        return failedResult(QString::fromLatin1(kErrorOutputDirectory), QStringLiteral("failed to create output directory"), error);
    const QString outputDirectory = QDir::cleanPath(QFileInfo(options.outputDirectory).absoluteFilePath());
    const QString finalBaseline = QDir(outputDirectory).filePath(QStringLiteral("baseline_robot_base.ply"));
    const QString finalRoi = QDir(outputDirectory).filePath(QStringLiteral("roi_template_robot_base.ply"));
    const QString finalMask = QDir(outputDirectory).filePath(QStringLiteral("plane_mask.png"));
    const QString finalJson = QDir(outputDirectory).filePath(QStringLiteral("temp_workpiece_info.json"));
    if (options.isCancelled && options.isCancelled())
        return failedResult(QStringLiteral("PCV_CANCELLED"), QStringLiteral("cancelled"), error);
    const QVector<pointcloud::Point3D> &roiPoints = matched.robotBasePoints;
    if (prepared.createdAtIso8601.trimmed().isEmpty())
        return failedResult(QString::fromLatin1(kErrorInputUnsupported),
                            QStringLiteral("created_at is required by the temporary workpiece contract"), error);
    const QString createdAt = prepared.createdAtIso8601;
    if (options.isCancelled && options.isCancelled())
        return failedResult(QStringLiteral("PCV_CANCELLED"), QStringLiteral("cancelled"), error);
    pcv::io::ArtifactTransaction transaction(outputDirectory,
                                              options.allowOverwrite,
                                              options.isCancelled);
    const QStringList robotBaseComments{QStringLiteral("source_frame robot_base")};
    const bool committed = transaction.stageBinaryPly(QStringLiteral("baseline_robot_base.ply"),
                                                       prepared.robotBasePoints, false,
                                                       robotBaseComments)
        && transaction.stageBinaryPly(QStringLiteral("roi_template_robot_base.ply"),
                                      roiPoints, false, robotBaseComments)
        && transaction.stageGrayscalePng(QStringLiteral("plane_mask.png"), options.planeMask)
        && transaction.stageJson(QStringLiteral("temp_workpiece_info.json"),
                                 metadataJson(options, createdAt,
                                              finalBaseline, finalRoi, finalMask))
        && transaction.commit();
    if (!committed) {
        detail = transaction.error();
        if (detail == QStringLiteral("已取消") || detail == QStringLiteral("cancelled")) {
            return failedResult(QStringLiteral("PCV_CANCELLED"), detail, error);
        }
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }
    TempWorkpieceResult result;
    result.success = true; result.outputDirectory = outputDirectory;
    result.tempWorkpieceInfoPath = finalJson; result.baselineRobotBasePly = finalBaseline;
    result.roiTemplateRobotBasePly = finalRoi; result.planeMaskPng = finalMask;
    result.createdAtIso8601 = createdAt; result.planePointCount = options.planeIndices.size();
    result.roiPointCount = matched.matchedPointCount; result.imageWidthPx = options.planeMask.width();
    result.imageHeightPx = options.planeMask.height(); result.imageWidthMm = result.imageWidthPx * 0.05;
    result.imageHeightMm = result.imageHeightPx * 0.05;
    return result;
}

} // namespace pcv::interface
