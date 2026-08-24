#include <pcv/interface/temp_workpiece_interface.h>

#include <pcv/infrastructure/runtime_paths.h>
#include <pcv/io/ply_reader.h>

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pcv::interface {
namespace {

constexpr double kPlaneToleranceMm = 0.4;
constexpr double kDefaultPixelSizeMm = 0.05;
constexpr qint64 kMaximumImagePixels = 100000000;

struct PlaneSelection {
    QVector3D origin;
    QVector3D axisX;
    QVector3D axisY;
    QVector3D axisZ;
    QVector3D normal;
    double d = 0.0;
    QVector<int> inlierIndices;
    QVector<int> roiIndices;
    QVector2D minimum;
    QVector2D maximum;
    QMatrix4x4 baseFromWorkpiece;
    QMatrix4x4 workpieceFromBase;
    pointcloud::RobotPose pose;
};

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

TempWorkpieceResult failedResult(const QString &code, const QString &message, QString *error)
{
    if (error) *error = QStringLiteral("%1: %2").arg(code, message);
    TempWorkpieceResult result;
    result.success = false;
    result.errorCode = code;
    result.message = message;
    return result;
}

QString scanningInfoErrorCode(const QString &detail)
{
    if (detail.startsWith(QStringLiteral("pose "))) {
        return QString::fromLatin1(kErrorPoseInvalid);
    }
    if (detail.startsWith(QStringLiteral("invalid JSON"))
        || detail.startsWith(QStringLiteral("failed to open temp_scanning_info.json"))) {
        return QString::fromLatin1(kErrorInputUnsupported);
    }
    return QString::fromLatin1(kErrorContract);
}

bool invalidComponent(const QString &value)
{
    const QString text = value.trimmed();
    bool hasWindowsNameCharacter = false;
    for (const QChar character : QStringLiteral("<>:\"|?*")) {
        if (text.contains(character)) {
            hasWindowsNameCharacter = true;
            break;
        }
    }
    return text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")
        || text.contains(QStringLiteral("..")) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || QFileInfo(text).isAbsolute()
        || hasWindowsNameCharacter || text.endsWith(QLatin1Char('.'))
        || text.endsWith(QLatin1Char(' '));
}

QString interfaceDirectory(const QString &runtimeRoot, const QString &jobId)
{
    return QDir(runtimeRoot).filePath(QStringLiteral("jobs/%1/interface").arg(jobId));
}

bool resolveRelativeToDirectory(const QString &directory,
                                const QString &input,
                                bool restrictToDirectory,
                                QString *resolved,
                                QString *error)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) return fail(error, QStringLiteral("file path is empty"));

    const QFileInfo fileInfo(text);
    if (fileInfo.isAbsolute()) {
        if (resolved) *resolved = QDir::cleanPath(fileInfo.absoluteFilePath());
        return true;
    }

    const QString cleanDirectory = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    const QString cleanPath = QDir::cleanPath(QFileInfo(QDir(directory).filePath(text)).absoluteFilePath());
    const bool insideDirectory = cleanPath.startsWith(cleanDirectory + QLatin1Char('/'), Qt::CaseInsensitive)
        || cleanPath.compare(cleanDirectory, Qt::CaseInsensitive) == 0;
    if (restrictToDirectory && !insideDirectory) {
        return fail(error, QStringLiteral("path escapes JSON directory: %1").arg(input));
    }
    if (resolved) *resolved = cleanPath;
    return true;
}

bool parsePoseArray(const QJsonArray &array, pointcloud::RobotPose *pose, QString *error)
{
    if (array.size() != 6) return fail(error, QStringLiteral("pose array must contain 6 numbers"));
    double values[6] = {};
    for (int i = 0; i < 6; ++i) {
        if (!array.at(i).isDouble()) return fail(error, QStringLiteral("pose contains non-number"));
        values[i] = array.at(i).toDouble();
        if (!std::isfinite(values[i])) return fail(error, QStringLiteral("pose contains non-finite number"));
    }
    pose->x = values[0];
    pose->y = values[1];
    pose->z = values[2];
    pose->rz = values[3];
    pose->ry = values[4];
    pose->rx = values[5];
    return true;
}

bool parsePoseObject(const QJsonObject &object, pointcloud::RobotPose *pose, QString *error)
{
    const QStringList keys{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                           QStringLiteral("RZ"), QStringLiteral("RY"), QStringLiteral("RX")};
    double values[6] = {};
    for (int i = 0; i < keys.size(); ++i) {
        const QJsonValue value = object.value(keys[i]);
        if (!value.isDouble()) return fail(error, QStringLiteral("pose object missing %1").arg(keys[i]));
        values[i] = value.toDouble();
        if (!std::isfinite(values[i])) return fail(error, QStringLiteral("pose contains non-finite number"));
    }
    pose->x = values[0];
    pose->y = values[1];
    pose->z = values[2];
    pose->rz = values[3];
    pose->ry = values[4];
    pose->rx = values[5];
    return true;
}

bool parsePose(const QJsonValue &value, pointcloud::RobotPose *pose, QString *error)
{
    if (value.isArray()) return parsePoseArray(value.toArray(), pose, error);
    if (value.isObject()) return parsePoseObject(value.toObject(), pose, error);
    return fail(error, QStringLiteral("pose must be an array or object"));
}

bool parseSeedIndices(const QJsonValue &value, QVector<int> *indices, QString *error)
{
    if (!value.isArray()) return fail(error, QStringLiteral("plane_seed_indices must be an array"));
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return fail(error, QStringLiteral("plane_seed_indices must contain exactly 3 indices"));
    }
    QVector<int> parsed;
    parsed.reserve(array.size());
    for (const QJsonValue item : array) {
        if (!item.isDouble()) return fail(error, QStringLiteral("plane_seed_indices contains non-number"));
        const double number = item.toDouble();
        if (!std::isfinite(number) || number < 0.0
            || number > double(std::numeric_limits<int>::max())
            || std::floor(number) != number) {
            return fail(error, QStringLiteral("plane_seed_indices contains invalid index"));
        }
        parsed.push_back(int(number));
    }
    if (indices) *indices = parsed;
    return true;
}

bool validateSeedIndices(const QVector<int> &indices, QString *error)
{
    if (indices.size() != 3) {
        return fail(error, QStringLiteral("plane seed indices must contain exactly 3 values"));
    }
    for (const int index : indices) {
        if (index < 0) return fail(error, QStringLiteral("plane seed indices must be non-negative"));
    }
    return true;
}

bool parseLayout(const QJsonObject &scan, TempScanningInfo *info, QString *error)
{
    const QJsonValue layoutValue = scan.value(QStringLiteral("point_cloud_layout"));
    if (layoutValue.isUndefined() || layoutValue.isNull()) {
        info->pointCloudLayout = pointcloud::DepthPointLayout::FullXyz;
        info->layoutWarning = QStringLiteral("point_cloud_layout missing; defaulted to FullXyz");
        return true;
    }
    if (!layoutValue.isString()) {
        return fail(error, QStringLiteral("point_cloud_layout must be a string"));
    }
    const QString text = layoutValue.toString();
    if (text.compare(QStringLiteral("FullXyz"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("full_xyz"), Qt::CaseInsensitive) == 0) {
        info->pointCloudLayout = pointcloud::DepthPointLayout::FullXyz;
        info->layoutWarning.clear();
        return true;
    }
    if (text.compare(QStringLiteral("LineProfileXz"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("line_profile_xz"), Qt::CaseInsensitive) == 0) {
        info->pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
        info->layoutWarning.clear();
        return true;
    }
    return fail(error, QStringLiteral("unsupported point_cloud_layout: %1").arg(text));
}

QString plyFormatName(pcv::detail::io::PlyFormat format)
{
    switch (format) {
    case pcv::detail::io::PlyFormat::Ascii: return QStringLiteral("ascii");
    case pcv::detail::io::PlyFormat::BinaryLittleEndian: return QStringLiteral("binary_little_endian");
    case pcv::detail::io::PlyFormat::BinaryBigEndian: return QStringLiteral("binary_big_endian");
    }
    return QStringLiteral("unknown");
}

QVector3D toVector(const pointcloud::Point3D &point)
{
    return {point.x, point.y, point.z};
}

bool finiteVector(const QVector3D &value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

QJsonArray vectorArray(const QVector3D &value)
{
    return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonArray poseArray(const pointcloud::RobotPose &pose)
{
    return QJsonArray{pose.x, pose.y, pose.z, pose.rz, pose.ry, pose.rx};
}

QJsonArray matrixArray(const QMatrix4x4 &matrix)
{
    QJsonArray rows;
    for (int row = 0; row < 4; ++row) {
        QJsonArray columns;
        for (int column = 0; column < 4; ++column)
            columns.push_back(matrix(row, column));
        rows.push_back(columns);
    }
    return rows;
}

bool buildPlaneSelection(const QVector<pointcloud::Point3D> &points,
                         const QVector<qsizetype> &sourceIndices,
                         const QVector<int> &seedSourceIndices,
                         int minimumPlaneInliers,
                         PlaneSelection *selection,
                         QString *error)
{
    if (seedSourceIndices.size() < 3) {
        return fail(error, QStringLiteral("at least 3 plane seed indices are required"));
    }
    if (points.size() != sourceIndices.size()) {
        return fail(error, QStringLiteral("source index mapping is inconsistent"));
    }

    QVector<int> seedConvertedIndices;
    seedConvertedIndices.reserve(3);
    for (int i = 0; i < 3; ++i) {
        int convertedIndex = -1;
        for (qsizetype j = 0; j < sourceIndices.size(); ++j) {
            if (sourceIndices[j] == seedSourceIndices[i]) {
                convertedIndex = int(j);
                break;
            }
        }
        if (convertedIndex < 0) {
            return fail(error, QStringLiteral("plane seed index was not converted"));
        }
        seedConvertedIndices.push_back(convertedIndex);
    }

    const QVector3D p0 = toVector(points[seedConvertedIndices[0]]);
    const QVector3D p1 = toVector(points[seedConvertedIndices[1]]);
    const QVector3D p2 = toVector(points[seedConvertedIndices[2]]);
    if (!finiteVector(p0) || !finiteVector(p1) || !finiteVector(p2)) {
        return fail(error, QStringLiteral("plane seeds contain invalid point"));
    }

    QVector3D axisX = p1 - p0;
    if (axisX.lengthSquared() <= 1.0e-12f) {
        return fail(error, QStringLiteral("plane X seed is too close to origin seed"));
    }
    axisX.normalize();

    const QVector3D seedY = p2 - p0;
    QVector3D axisZ = QVector3D::crossProduct(axisX, seedY);
    if (axisZ.lengthSquared() <= 1.0e-12f) {
        return fail(error, QStringLiteral("plane seeds are collinear"));
    }
    axisZ.normalize();
    QVector3D axisY = QVector3D::crossProduct(axisZ, axisX);
    if (axisY.lengthSquared() <= 1.0e-12f) {
        return fail(error, QStringLiteral("workpiece Y axis is degenerate"));
    }
    axisY.normalize();

    const double d = -QVector3D::dotProduct(axisZ, p0);
    QVector<int> inliers;
    inliers.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const QVector3D p = toVector(points[i]);
        if (!finiteVector(p)) continue;
        const double distance = std::abs(QVector3D::dotProduct(axisZ, p) + d);
        if (distance <= kPlaneToleranceMm) inliers.push_back(i);
    }
    const int requiredInliers = minimumPlaneInliers > 0 ? minimumPlaneInliers : 3;
    if (inliers.size() < requiredInliers) {
        return fail(error, QStringLiteral("plane inliers are insufficient"));
    }

    QVector2D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector2D maximum(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (int index : inliers) {
        const QVector3D p = toVector(points[index]);
        const QVector3D delta = p - p0;
        const float u = QVector3D::dotProduct(delta, axisX);
        const float v = QVector3D::dotProduct(delta, axisY);
        minimum.setX(qMin(minimum.x(), u));
        minimum.setY(qMin(minimum.y(), v));
        maximum.setX(qMax(maximum.x(), u));
        maximum.setY(qMax(maximum.y(), v));
    }
    if (!std::isfinite(minimum.x()) || !std::isfinite(minimum.y())
        || !std::isfinite(maximum.x()) || !std::isfinite(maximum.y())
        || maximum.x() < minimum.x() || maximum.y() < minimum.y()) {
        return fail(error, QStringLiteral("ROI bounds are invalid"));
    }

    QVector<int> roiIndices;
    roiIndices.reserve(inliers.size());
    constexpr float epsilon = 1.0e-4f;
    for (int index : inliers) {
        const QVector3D p = toVector(points[index]);
        const QVector3D delta = p - p0;
        const float u = QVector3D::dotProduct(delta, axisX);
        const float v = QVector3D::dotProduct(delta, axisY);
        if (u >= minimum.x() - epsilon && u <= maximum.x() + epsilon
            && v >= minimum.y() - epsilon && v <= maximum.y() + epsilon) {
            roiIndices.push_back(index);
        }
    }
    if (roiIndices.size() < requiredInliers) {
        return fail(error, QStringLiteral("ROI points are insufficient"));
    }

    QMatrix4x4 baseFromWorkpiece;
    baseFromWorkpiece.setToIdentity();
    for (int row = 0; row < 3; ++row) {
        baseFromWorkpiece(row, 0) = axisX[row];
        baseFromWorkpiece(row, 1) = axisY[row];
        baseFromWorkpiece(row, 2) = axisZ[row];
        baseFromWorkpiece(row, 3) = p0[row];
    }
    bool invertible = false;
    const QMatrix4x4 workpieceFromBase = baseFromWorkpiece.inverted(&invertible);
    if (!invertible) return fail(error, QStringLiteral("workpiece frame is not invertible"));

    selection->origin = p0;
    selection->axisX = axisX;
    selection->axisY = axisY;
    selection->axisZ = axisZ;
    selection->normal = axisZ;
    selection->d = d;
    selection->inlierIndices = inliers;
    selection->roiIndices = roiIndices;
    selection->minimum = minimum;
    selection->maximum = maximum;
    selection->baseFromWorkpiece = baseFromWorkpiece;
    selection->workpieceFromBase = workpieceFromBase;
    selection->pose = pointcloud::matrixToRobotPose(baseFromWorkpiece);
    return true;
}

bool buildPlaneMask(const PlaneSelection &selection,
                    int *widthPx,
                    int *heightPx,
                    QImage *image,
                    QString *error)
{
    const double widthMm = double(selection.maximum.x()) - double(selection.minimum.x());
    const double heightMm = double(selection.maximum.y()) - double(selection.minimum.y());
    if (!std::isfinite(widthMm) || !std::isfinite(heightMm) || widthMm < 0.0 || heightMm < 0.0) {
        return fail(error, QStringLiteral("ROI physical size is invalid"));
    }
    const int width = qMax(1, int(std::ceil(widthMm / kDefaultPixelSizeMm)));
    const int height = qMax(1, int(std::ceil(heightMm / kDefaultPixelSizeMm)));
    if (qint64(width) * qint64(height) > kMaximumImagePixels) {
        return fail(error, QStringLiteral("ROI image exceeds maximum pixel count"));
    }

    QImage mask(width, height, QImage::Format_Grayscale8);
    if (mask.isNull()) return fail(error, QStringLiteral("failed to allocate plane mask"));
    mask.fill(0);
    for (int y = 0; y < height; ++y) {
        uchar *row = mask.scanLine(y);
        for (int x = 0; x < width; ++x) row[x] = 255;
    }
    *widthPx = width;
    *heightPx = height;
    *image = mask;
    return true;
}

QVector<pointcloud::Point3D> selectPoints(const QVector<pointcloud::Point3D> &points,
                                          const QVector<int> &indices,
                                          const QVector3D &normal)
{
    QVector<pointcloud::Point3D> selected;
    selected.reserve(indices.size());
    for (int index : indices) {
        if (index < 0 || index >= points.size()) continue;
        pointcloud::Point3D point = points[index];
        point.nx = normal.x();
        point.ny = normal.y();
        point.nz = normal.z();
        selected.push_back(point);
    }
    return selected;
}

bool writeRobotBasePly(const QString &path,
                       const QVector<pointcloud::Point3D> &points,
                       const QVector<qsizetype> *sourceIndices,
                       QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream header(&file);
    header.setEncoding(QStringConverter::Utf8);
    header << "ply\nformat binary_little_endian 1.0\n"
           << "comment source_frame robot_base\n"
           << "comment target_frame robot_base\n"
           << "element vertex " << points.size() << "\n"
           << "property float x\nproperty float y\nproperty float z\n"
           << "property float nx\nproperty float ny\nproperty float nz\n"
           << "property uint source_index\nend_header\n";
    header.flush();
    if (header.status() != QTextStream::Ok) {
        if (error) *error = QStringLiteral("failed to write PLY header");
        return false;
    }

    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (qsizetype i = 0; i < points.size(); ++i) {
        const pointcloud::Point3D &point = points[i];
        const quint32 sourceIndex = sourceIndices && i < sourceIndices->size()
            ? quint32(qMax<qsizetype>(0, sourceIndices->at(i))) : quint32(i);
        data << point.x << point.y << point.z << point.nx << point.ny << point.nz
             << sourceIndex;
    }
    if (data.status() != QDataStream::Ok || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("failed to write PLY payload") : file.errorString();
        return false;
    }
    return true;
}

bool writePng(const QString &path, const QImage &image, QString *error)
{
    if (image.isNull() || image.format() != QImage::Format_Grayscale8) {
        return fail(error, QStringLiteral("plane mask image is invalid"));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("failed to write PNG") : file.errorString();
        return false;
    }
    return true;
}

bool writeJsonFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("failed to write JSON") : file.errorString();
        return false;
    }
    return true;
}

QString quotedJson(const QString &value)
{
    const QString array = QString::fromUtf8(
        QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
    return array.mid(1, array.size() - 2);
}

QString jsonNumber(double value)
{
    return QString::number(value, 'g', 15);
}

QString poseJson(const pointcloud::RobotPose &pose)
{
    return QStringLiteral("[%1, %2, %3, %4, %5, %6]")
        .arg(jsonNumber(pose.x), jsonNumber(pose.y), jsonNumber(pose.z),
             jsonNumber(pose.rz), jsonNumber(pose.ry), jsonNumber(pose.rx));
}

QByteArray buildOutputJson(const QString &createdAt,
                           const PlaneSelection &selection,
                           int widthPx,
                           int heightPx,
                           const QString &baselinePath,
                           const QString &roiPath,
                           const QString &planeMaskPath)
{
    const double widthMm = double(widthPx) * kDefaultPixelSizeMm;
    const double heightMm = double(heightPx) * kDefaultPixelSizeMm;
    const QString absoluteBaselinePath = QDir::fromNativeSeparators(
        QFileInfo(baselinePath).absoluteFilePath());
    const QString absoluteRoiPath = QDir::fromNativeSeparators(
        QFileInfo(roiPath).absoluteFilePath());
    const QString absolutePlaneMaskPath = QDir::fromNativeSeparators(
        QFileInfo(planeMaskPath).absoluteFilePath());
    QString text;
    QTextStream stream(&text);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "{\n"
           << "  \"schema_version\": " << quotedJson(QString::fromLatin1(kTempWorkpieceSchema)) << ",\n"
           << "  \"kind\": " << quotedJson(QString::fromLatin1(kTempWorkpieceKind)) << ",\n"
           << "  \"created_at\": " << quotedJson(createdAt) << ",\n"
           << "  \"plane\": {\n"
           << "    \"name\": " << quotedJson(QString::fromLatin1(kTempPlaneName)) << ",\n"
           << "    \"equation\": " << poseJson(selection.pose) << "\n"
           << "  },\n"
           << "  \"image\": {\n"
           << "    \"name\": " << quotedJson(QFileInfo(planeMaskPath).fileName()) << ",\n"
           << "    \"width_px\": " << widthPx << ",\n"
           << "    \"height_px\": " << heightPx << ",\n"
           << "    \"width_mm\": " << jsonNumber(widthMm) << ",\n"
           << "    \"height_mm\": " << jsonNumber(heightMm) << ",\n"
           << "    \"pixel_size_mm\": " << jsonNumber(kDefaultPixelSizeMm) << "\n"
           << "  },\n"
           << "  \"roi\": \"rectangle\",\n"
           << "  \"outputs\": {\n"
           << "    \"robot_base_point_cloud\": " << quotedJson(absoluteBaselinePath) << ",\n"
           << "    \"roi_point_cloud\": " << quotedJson(absoluteRoiPath) << ",\n"
           << "    \"plane_mask\": " << quotedJson(absolutePlaneMaskPath) << "\n"
           << "  }\n"
           << "}\n";
    stream.flush();
    return text.toUtf8();
}

bool commitStagedFiles(const QVector<QPair<QString, QString>> &files, QString *error)
{
    struct CommitFile {
        QString staged;
        QString final;
        QString backup;
        bool hadOriginal = false;
        bool committed = false;
    };
    QVector<CommitFile> commits;
    commits.reserve(files.size());
    const QString stagingDir = QFileInfo(files.first().first).absolutePath();
    for (const auto &file : files) {
        commits.push_back({file.first, file.second, QString(), false, false});
    }

    auto rollback = [&commits]() {
        for (const CommitFile &file : commits) {
            if (file.committed) QFile::remove(file.final);
        }
        for (const CommitFile &file : commits) {
            if (file.hadOriginal) QFile::rename(file.backup, file.final);
        }
    };

    for (int i = 0; i < commits.size(); ++i) {
        CommitFile &file = commits[i];
        if (!QFileInfo::exists(file.final)) continue;
        if (!QFileInfo(file.final).isFile()) {
            if (error) *error = QStringLiteral("final output path is not a file: %1").arg(file.final);
            return false;
        }
        file.backup = QDir(stagingDir).filePath(QStringLiteral("backup_%1").arg(i));
        if (!QFile::rename(file.final, file.backup)) {
            rollback();
            if (error) *error = QStringLiteral("failed to stage existing output: %1").arg(file.final);
            return false;
        }
        file.hadOriginal = true;
    }

    for (CommitFile &file : commits) {
        if (!QFile::rename(file.staged, file.final)) {
            rollback();
            if (error) *error = QStringLiteral("failed to commit output: %1").arg(file.final);
            return false;
        }
        file.committed = true;
    }

    for (const CommitFile &file : commits) {
        if (file.hadOriginal) QFile::remove(file.backup);
    }
    return true;
}

} // namespace

QString defaultRuntimeRoot()
{
    return QDir(pcv::runtime::applicationDataDirectory()).filePath(QStringLiteral("runtime_data"));
}

bool parseTempScanningInfo(const QString &filePath,
                           TempScanningInfo *info,
                           QString *error)
{
    if (!info) return fail(error, QStringLiteral("TempScanningInfo output is null"));
    *info = {};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("failed to open temp_scanning_info.json: %1").arg(filePath));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(error, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toString() != QString::fromLatin1(kTempScanningSchema)) {
        return fail(error, QStringLiteral("unsupported schema_version"));
    }
    if (root.value(QStringLiteral("kind")).toString() != QString::fromLatin1(kTempScanningKind)) {
        return fail(error, QStringLiteral("unsupported kind"));
    }

    const QJsonObject scan = root.value(QStringLiteral("scan")).toObject();
    const QJsonObject calibration = root.value(QStringLiteral("calibration")).toObject();
    if (scan.isEmpty() || calibration.isEmpty()) {
        return fail(error, QStringLiteral("scan/calibration object is required"));
    }

    info->schemaVersion = root.value(QStringLiteral("schema_version")).toString();
    info->kind = root.value(QStringLiteral("kind")).toString();
    info->scanId = scan.value(QStringLiteral("scan_id")).toString();
    info->pointCloudFile = scan.value(QStringLiteral("point_cloud_file")).toString();
    info->coordinateFrame = scan.value(QStringLiteral("coordinate_frame")).toString();
    info->calibrationFile = calibration.value(QStringLiteral("calibration_file")).toString();
    info->calibrationSourceFrame = calibration.value(QStringLiteral("source_frame")).toString();
    info->calibrationTargetFrame = calibration.value(QStringLiteral("target_frame")).toString();
    info->layoutWarning.clear();

    if (info->schemaVersion != QString::fromLatin1(kTempScanningSchema)) {
        return fail(error, QStringLiteral("unsupported schema_version"));
    }
    if (info->kind != QString::fromLatin1(kTempScanningKind)) {
        return fail(error, QStringLiteral("unsupported kind"));
    }
    if (info->scanId.trimmed().isEmpty()) return fail(error, QStringLiteral("scan_id is required"));
    if (info->pointCloudFile.trimmed().isEmpty()) return fail(error, QStringLiteral("point_cloud_file is required"));
    if (info->coordinateFrame.trimmed().isEmpty()) return fail(error, QStringLiteral("coordinate_frame is required"));
    if (info->calibrationFile.trimmed().isEmpty()) return fail(error, QStringLiteral("calibration_file is required"));
    if (info->calibrationSourceFrame.trimmed().isEmpty()) return fail(error, QStringLiteral("source_frame is required"));
    if (info->calibrationTargetFrame.trimmed().isEmpty()) return fail(error, QStringLiteral("target_frame is required"));
    if (info->coordinateFrame != QStringLiteral("camera")) {
        return fail(error, QStringLiteral("scan coordinate_frame must be camera"));
    }
    if (info->calibrationSourceFrame != QStringLiteral("camera")
        || info->calibrationTargetFrame != QStringLiteral("robot_base")) {
        return fail(error, QStringLiteral("calibration frames must be camera -> robot_base"));
    }

    if (!parseLayout(scan, info, error)) return false;
    if (!scan.contains(QStringLiteral("robot_pose_start"))) {
        return fail(error, QStringLiteral("robot_pose_start is required"));
    }
    if (!scan.contains(QStringLiteral("robot_pose_end"))) {
        return fail(error, QStringLiteral("robot_pose_end is required"));
    }
    if (!parsePose(scan.value(QStringLiteral("robot_pose_start")), &info->robotPoseStart, error)) return false;
    if (!parsePose(scan.value(QStringLiteral("robot_pose_end")), &info->robotPoseEnd, error)) return false;
    if (!scan.contains(QStringLiteral("plane_seed_indices"))) {
        return fail(error, QStringLiteral("plane_seed_indices is required"));
    }
    if (!parseSeedIndices(scan.value(QStringLiteral("plane_seed_indices")),
                          &info->planeSeedIndices, error)) {
        return false;
    }
    const QString jsonDirectory = QFileInfo(filePath).absolutePath();
    QString resolvedPath;
    if (!resolveRelativeToDirectory(jsonDirectory, info->pointCloudFile, true,
                                    &resolvedPath, error)
        || !resolveRelativeToDirectory(jsonDirectory, info->calibrationFile, true,
                                       &resolvedPath, error)) {
        return false;
    }

    info->valid = true;
    return true;
}

TempWorkpieceResult generateTempWorkpiece(const TempWorkpieceOptions &options,
                                          QString *error)
{
    const bool hasExplicitScanningInfoPath = !options.scanningInfoPath.trimmed().isEmpty();
    const QString runtimeRoot = options.runtimeRoot.trimmed().isEmpty()
        ? defaultRuntimeRoot() : QDir::cleanPath(options.runtimeRoot);
    const bool invalidExplicitJobId = !options.jobId.trimmed().isEmpty()
        && invalidComponent(options.jobId);
    if (runtimeRoot.trimmed().isEmpty()
        || (!hasExplicitScanningInfoPath && invalidComponent(options.jobId))
        || invalidExplicitJobId) {
        return failedResult(QString::fromLatin1(kErrorContract),
                            QStringLiteral("runtimeRoot/jobId is invalid"), error);
    }

    QString scanningInfoPath = options.scanningInfoPath.trimmed();
    QString jsonDirectory;
    if (scanningInfoPath.isEmpty()) {
        jsonDirectory = interfaceDirectory(runtimeRoot, options.jobId);
        scanningInfoPath = QDir(jsonDirectory).filePath(QStringLiteral("temp_scanning_info.json"));
        if (!QFileInfo::exists(scanningInfoPath)) {
            return failedResult(QString::fromLatin1(kErrorInputMissing),
                                QStringLiteral("temp_scanning_info.json is missing"), error);
        }
    } else {
        scanningInfoPath = QDir::cleanPath(QFileInfo(scanningInfoPath).absoluteFilePath());
        jsonDirectory = QFileInfo(scanningInfoPath).absolutePath();
    }
    if (!QFileInfo::exists(scanningInfoPath)) {
        return failedResult(QString::fromLatin1(kErrorInputMissing),
                            QStringLiteral("temp_scanning_info.json is missing"), error);
    }

    QString detail;
    QString outputDirectory = options.outputDirectory.trimmed();
    if (outputDirectory.isEmpty()) {
        outputDirectory = jsonDirectory;
    } else {
        QString resolvedOutputDirectory;
        if (!resolveRelativeToDirectory(jsonDirectory, outputDirectory, true,
                                        &resolvedOutputDirectory, &detail)) {
            return failedResult(QString::fromLatin1(kErrorContract), detail, error);
        }
        outputDirectory = resolvedOutputDirectory;
    }

    TempScanningInfo scan;
    if (!parseTempScanningInfo(scanningInfoPath, &scan, &detail)) {
        return failedResult(scanningInfoErrorCode(detail), detail, error);
    }
    QString resolvedPointCloud;
    if (!resolveRelativeToDirectory(jsonDirectory, scan.pointCloudFile, true, &resolvedPointCloud, &detail)) {
        return failedResult(QString::fromLatin1(kErrorInputUnsupported), detail, error);
    }
    if (!QFileInfo::exists(resolvedPointCloud)) {
        return failedResult(QString::fromLatin1(kErrorInputMissing),
                            QStringLiteral("point cloud file is missing"), error);
    }
    QString resolvedCalibration;
    if (!resolveRelativeToDirectory(jsonDirectory, scan.calibrationFile, true, &resolvedCalibration, &detail)) {
        return failedResult(QString::fromLatin1(kErrorInputUnsupported), detail, error);
    }
    if (!QFileInfo::exists(resolvedCalibration)) {
        return failedResult(QString::fromLatin1(kErrorInputMissing),
                            QStringLiteral("calibration file is missing"), error);
    }

    const pcv::detail::io::PlyReadResult ply = pcv::detail::io::readPly(resolvedPointCloud);
    if (!ply.ok) {
        return failedResult(QString::fromLatin1(kErrorInputUnsupported), ply.error, error);
    }

    pointcloud::HandEyeCalibration calibration;
    if (!pointcloud::loadHandEyeCalibration(resolvedCalibration, &calibration, &detail)) {
        return failedResult(QString::fromLatin1(kErrorTransformInvalid), detail, error);
    }

    pointcloud::CloudTransformOptions transformOptions;
    transformOptions.layout = scan.pointCloudLayout;
    transformOptions.sampleStride = 1;
    const pointcloud::RobotCloudResult cloud = pointcloud::transformLineScanToRobotBase(
        ply.points, calibration, scan.robotPoseStart, scan.robotPoseEnd, transformOptions);
    if (!cloud.ok) {
        return failedResult(QString::fromLatin1(kErrorTransformInvalid), cloud.error, error);
    }
    const QVector<int> seedIndices = options.planeSeedIndices.isEmpty()
        ? scan.planeSeedIndices
        : options.planeSeedIndices;
    if (!validateSeedIndices(seedIndices, &detail)) {
        return failedResult(QString::fromLatin1(kErrorContract), detail, error);
    }

    PlaneSelection selection;
    if (!buildPlaneSelection(cloud.points, cloud.sourceIndices, seedIndices,
                             options.minimumPlaneInliers, &selection, &detail)) {
        const QString code = detail.contains(QStringLiteral("invertible"))
            ? QString::fromLatin1(kErrorFrame) : QString::fromLatin1(kErrorPlane);
        return failedResult(code, detail, error);
    }

    int widthPx = 0;
    int heightPx = 0;
    QImage planeMask;
    if (!buildPlaneMask(selection, &widthPx, &heightPx, &planeMask, &detail)) {
        return failedResult(QString::fromLatin1(kErrorImage), detail, error);
    }

    if (!QDir().mkpath(outputDirectory)) {
        return failedResult(QString::fromLatin1(kErrorOutputDirectory),
                            QStringLiteral("failed to create output directory"), error);
    }
    QTemporaryDir staging(QDir(outputDirectory).filePath(QStringLiteral(".temp-workpiece-staging-XXXXXX")));
    if (!staging.isValid()) {
        return failedResult(QString::fromLatin1(kErrorOutputDirectory),
                            QStringLiteral("failed to create staging directory"), error);
    }

    const QString stagedBaseline = staging.filePath(QStringLiteral("baseline_robot_base.ply"));
    const QString stagedRoi = staging.filePath(QStringLiteral("roi_template_robot_base.ply"));
    const QString stagedMask = staging.filePath(QStringLiteral("plane_mask.png"));
    const QString stagedJson = staging.filePath(QStringLiteral("temp_workpiece_info.json"));

    if (!writeRobotBasePly(stagedBaseline, cloud.points, &cloud.sourceIndices, &detail)) {
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }
    const QVector<pointcloud::Point3D> roiPoints = selectPoints(cloud.points, selection.roiIndices, selection.normal);
    QVector<qsizetype> roiSourceIndices;
    roiSourceIndices.reserve(selection.roiIndices.size());
    for (int index : selection.roiIndices) roiSourceIndices.push_back(cloud.sourceIndices[index]);
    if (!writeRobotBasePly(stagedRoi, roiPoints, &roiSourceIndices, &detail)) {
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }
    if (!writePng(stagedMask, planeMask, &detail)) {
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }

    const QString finalBaseline = QDir(outputDirectory).filePath(QStringLiteral("baseline_robot_base.ply"));
    const QString finalRoi = QDir(outputDirectory).filePath(QStringLiteral("roi_template_robot_base.ply"));
    const QString finalMask = QDir(outputDirectory).filePath(QStringLiteral("plane_mask.png"));
    const QString finalJson = QDir(outputDirectory).filePath(QStringLiteral("temp_workpiece_info.json"));
    const QString createdAt = options.createdAtIso8601.trimmed().isEmpty()
        ? QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        : options.createdAtIso8601;
    const QByteArray outputJson = buildOutputJson(createdAt, selection, widthPx, heightPx,
                                                  finalBaseline, finalRoi, finalMask);
    if (!writeJsonFile(stagedJson, outputJson, &detail)) {
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }

    const QVector<QPair<QString, QString>> commits{
        {stagedBaseline, finalBaseline},
        {stagedRoi, finalRoi},
        {stagedMask, finalMask},
        {stagedJson, finalJson}
    };
    if (!commitStagedFiles(commits, &detail)) {
        return failedResult(QString::fromLatin1(kErrorOutputIncomplete), detail, error);
    }

    TempWorkpieceResult result;
    result.success = true;
    result.runtimeRoot = runtimeRoot;
    result.jobId = options.jobId;
    result.interfaceDirectory = outputDirectory;
    result.scanningInfoPath = scanningInfoPath;
    result.tempWorkpieceInfoPath = finalJson;
    result.baselineRobotBasePly = finalBaseline;
    result.roiTemplateRobotBasePly = finalRoi;
    result.planeMaskPng = finalMask;
    result.scanId = scan.scanId;
    result.schemaVersion = QString::fromLatin1(kTempWorkpieceSchema);
    result.kind = QString::fromLatin1(kTempWorkpieceKind);
    result.sourceFrame = scan.coordinateFrame;
    result.targetFrame = scan.calibrationTargetFrame;
    result.coordinateFrame = QStringLiteral("robot_base");
    result.calibrationSourceFrame = scan.calibrationSourceFrame;
    result.calibrationTargetFrame = scan.calibrationTargetFrame;
    result.robotPoseStart = scan.robotPoseStart;
    result.robotPoseEnd = scan.robotPoseEnd;
    result.warning = scan.layoutWarning;
    result.resolvedPointCloudFile = resolvedPointCloud;
    result.resolvedCalibrationFile = resolvedCalibration;
    result.pointCloudLayout = scan.pointCloudLayout;
    result.declaredPointCount = ply.declaredPointCount;
    result.convertedPointCount = cloud.points.size();
    result.rejectedInvalidPointCount = cloud.rejectedInvalid;
    result.rejectedRangePointCount = cloud.rejectedRange;
    result.planePointCount = selection.inlierIndices.size();
    result.roiPointCount = selection.roiIndices.size();
    result.imageWidthPx = widthPx;
    result.imageHeightPx = heightPx;
    result.imageWidthMm = double(widthPx) * kDefaultPixelSizeMm;
    result.imageHeightMm = double(heightPx) * kDefaultPixelSizeMm;
    result.pixelSizeMm = kDefaultPixelSizeMm;
    return result;
}

} // namespace pcv::interface
