#include <pcv/output/plane_output.h>

#include <pcv/infrastructure/runtime_paths.h>

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace {

bool invalidComponent(const QString &value)
{
    const QString text = value.trimmed();
    bool hasWindowsNameCharacter = false;
    for (const QChar character : QStringLiteral("<>:\"|?*")) {
        if (text.contains(character)) { hasWindowsNameCharacter = true; break; }
    }
    return text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")
        || text.contains(QStringLiteral("..")) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || QFileInfo(text).isAbsolute()
        || hasWindowsNameCharacter || text.endsWith(QLatin1Char('.'))
        || text.endsWith(QLatin1Char(' '));
}

bool writeImage(const QString &path, const QImage &source, QString *error)
{
    QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    if (image.isNull()) {
        if (error) *error = QStringLiteral("无法转换为 Grayscale8");
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) row[x] = row[x] == 0 ? 0 : 255;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("PNG 写入失败") : file.errorString();
        return false;
    }
    return true;
}

bool writePly(const QString &path, const QVector<pointcloud::Point3D> &points,
              const QVector<int> &indices, const pcv::output::PlaneOutputMetadata &metadata,
              qsizetype *count, QString *error)
{
    QVector<int> valid;
    valid.reserve(indices.size());
    for (int index : indices)
        if (index >= 0 && index < points.size() && std::isfinite(points[index].x)
            && std::isfinite(points[index].y) && std::isfinite(points[index].z)) valid.push_back(index);
    if (valid.isEmpty()) {
        if (error) *error = QStringLiteral("平面索引中没有有效点");
        return false;
    }
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
           << "comment source_ply_encoding " << metadata.sourcePlyEncoding << "\n"
           << "element vertex " << valid.size() << "\n"
           << "property float x\nproperty float y\nproperty float z\n"
           << "property float nx\nproperty float ny\nproperty float nz\n"
           << "property uint source_index\nend_header\n";
    header.flush();
    if (header.status() != QTextStream::Ok) {
        if (error) *error = QStringLiteral("写入 PLY Header 失败");
        return false;
    }
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (int index : valid) {
        const auto &point = points[index];
        data << point.x << point.y << point.z << point.nx << point.ny << point.nz
             << quint32(index);
    }
    if (data.status() != QDataStream::Ok || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("PLY 写入失败") : file.errorString();
        return false;
    }
    if (count) *count = valid.size();
    return true;
}

bool writeJson(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("JSON 写入不完整") : file.errorString();
        return false;
    }
    return true;
}

} // namespace

namespace pcv::output {

QString defaultRuntimeRoot()
{
    return QDir(pcv::runtime::applicationDataDirectory()).filePath(QStringLiteral("runtime_data"));
}

bool validateJobContext(const JobContext &context, QString *error)
{
    if (context.runtimeRoot.trimmed().isEmpty() || invalidComponent(context.jobId)
        || invalidComponent(context.workpieceId) || invalidComponent(context.baseName)) {
        if (error) *error = QStringLiteral("PCV_CONTRACT_001: job_id/workpiece_id/base_name/runtime_root 参数无效");
        return false;
    }
    return true;
}

QString jobRoot(const JobContext &context)
{
    return QDir(context.runtimeRoot).filePath(QStringLiteral("jobs/%1").arg(context.jobId));
}

PlaneOutputResult writePlaneOutput(const JobContext &context, const QImage &image,
                                   const QVector<pointcloud::Point3D> &points,
                                   const QVector<int> &planeIndices,
                                   const PlaneOutputMetadata &metadata)
{
    PlaneOutputResult result;
    QString validationError;
    if (!validateJobContext(context, &validationError)) {
        result.errorCode = QStringLiteral("PCV_CONTRACT_001"); result.message = validationError; return result;
    }
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        result.errorCode = QStringLiteral("PCV_IMAGE_001"); result.message = QStringLiteral("图像为空或尺寸无效"); return result;
    }
    if (planeIndices.size() < 3 || points.isEmpty()) {
        result.errorCode = QStringLiteral("PCV_PLANE_001"); result.message = QStringLiteral("平面点不足或拟合失败"); return result;
    }
    for (int index : planeIndices) {
        if (index < 0 || index >= points.size()
            || !std::isfinite(points[index].x) || !std::isfinite(points[index].y)
            || !std::isfinite(points[index].z)) {
            result.errorCode = QStringLiteral("PCV_PLANE_001");
            result.message = QStringLiteral("平面索引越界或包含无效点");
            return result;
        }
    }
    QMatrix4x4 base = metadata.TBaseWorkpiece;
    QMatrix4x4 inverse = metadata.TWorkpieceBase;
    if (!std::isfinite(base.determinant()) || !std::isfinite(inverse.determinant())
        || std::abs(base.determinant()) < 1.0e-8f
        || std::abs(inverse.determinant()) < 1.0e-8f) {
        result.errorCode = QStringLiteral("PCV_FRAME_001");
        result.message = QStringLiteral("工件坐标系矩阵不可逆");
        return result;
    }
    const bool directDestination = !context.destinationDirectory.trimmed().isEmpty();
    const QString root = directDestination
        ? QDir::cleanPath(context.destinationDirectory) : jobRoot(context);
    QDir directory(root);
    const bool directoriesReady = directDestination
        ? QDir().mkpath(root)
        : (directory.mkpath(QStringLiteral("point_cloud/raw"))
           && directory.mkpath(QStringLiteral("point_cloud/stitched"))
           && directory.mkpath(QStringLiteral("point_cloud/plane"))
           && directory.mkpath(QStringLiteral("point_cloud/reports"))
           && directory.mkpath(QStringLiteral("high_points"))
           && directory.mkpath(QStringLiteral("paths"))
           && directory.mkpath(QStringLiteral("logs")));
    if (!directoriesReady) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_001"); result.message = QStringLiteral("运行目录创建失败"); return result;
    }
    const QString outputPrefix = directDestination ? QString() : QStringLiteral("point_cloud/plane/");
    const QString pngRelative = QStringLiteral("%1%2.png").arg(outputPrefix, context.baseName);
    const QString jsonRelative = QStringLiteral("%1%2.json").arg(outputPrefix, context.baseName);
    const QString plyRelative = QStringLiteral("%1%2_plane_robot_base.ply").arg(outputPrefix, context.baseName);
    const QString pngPath = directory.filePath(pngRelative);
    const QString jsonPath = directory.filePath(jsonRelative);
    const QString plyPath = directory.filePath(plyRelative);
    QTemporaryDir staging(QDir(root).filePath(
        QStringLiteral(".%1-plane-staging-XXXXXX").arg(context.baseName)));
    if (!staging.isValid()) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_001");
        result.message = QStringLiteral("无法创建输出临时目录");
        return result;
    }
    const QString stagedPng = staging.filePath(QStringLiteral("plane.png"));
    const QString stagedPly = staging.filePath(QStringLiteral("plane.ply"));
    const QString stagedJson = staging.filePath(QStringLiteral("plane.json"));
    QString error;
    if (!writeImage(stagedPng, image, &error)) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = error; return result;
    }
    qsizetype count = 0;
    if (!writePly(stagedPly, points, planeIndices, metadata, &count, &error)) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = error; return result;
    }
    const double pixelSize = metadata.pixelSizeMm > 0.0 ? metadata.pixelSizeMm : 0.05;
    const QString sourcePointCloud = QDir::fromNativeSeparators(
        QFileInfo(metadata.sourcePointCloud).absoluteFilePath());
    const QString absolutePngPath = QDir::fromNativeSeparators(QFileInfo(pngPath).absoluteFilePath());
    const QString absolutePlyPath = QDir::fromNativeSeparators(QFileInfo(plyPath).absoluteFilePath());
    // The frame stores angles as A/B/C fields; formal JSON emits the
    // controller order requested by the contract as C,B,A.
    const QJsonArray poseEquation{
        metadata.originInRobotBase.x(), metadata.originInRobotBase.y(), metadata.originInRobotBase.z(),
        metadata.abcDeg.z(), metadata.abcDeg.y(), metadata.abcDeg.x()};
    const QJsonObject imageObject{
        {QStringLiteral("name"), QFileInfo(pngPath).fileName()},
        {QStringLiteral("width_px"), image.width()},
        {QStringLiteral("height_px"), image.height()},
        {QStringLiteral("width_mm"), double(image.width()) * pixelSize},
        {QStringLiteral("height_mm"), double(image.height()) * pixelSize},
        {QStringLiteral("pixel_size_mm"), pixelSize}};
    QJsonObject rootObject{
        {QStringLiteral("schema_version"), QString::fromLatin1(kPlaneOutputSchema)},
        {QStringLiteral("kind"), QStringLiteral("single_frame_workpiece_roi")},
        {QStringLiteral("created_at"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("plane"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("WObj1")},
            {QStringLiteral("equation"), poseEquation}}},
        {QStringLiteral("image"), imageObject},
        {QStringLiteral("roi"), QStringLiteral("rectangle")},
        {QStringLiteral("outputs"), QJsonObject{
            {QStringLiteral("robot_base_point_cloud"), sourcePointCloud},
            {QStringLiteral("roi_point_cloud"), absolutePlyPath},
            {QStringLiteral("plane_mask"), absolutePngPath}}}
    };
    const QByteArray bytes = QJsonDocument(rootObject).toJson(QJsonDocument::Indented);
    if (!writeJson(stagedJson, bytes, &error)) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = error; return result;
    }
    struct CommitFile {
        QString staged;
        QString final;
        QString backup;
        bool hadOriginal = false;
        bool committed = false;
    };
    QVector<CommitFile> files{{stagedPng, pngPath, {}, false, false},
                              {stagedPly, plyPath, {}, false, false},
                              {stagedJson, jsonPath, {}, false, false}};
    auto rollback = [&files]() {
        for (auto &file : files) {
            if (file.committed) QFile::remove(file.final);
        }
        for (auto &file : files) {
            if (file.hadOriginal) QFile::rename(file.backup, file.final);
        }
    };
    for (int i = 0; i < files.size(); ++i) {
        auto &file = files[i];
        if (!QFileInfo::exists(file.final)) continue;
        file.backup = staging.filePath(QStringLiteral("backup_%1").arg(i));
        if (!QFile::rename(file.final, file.backup)) {
            rollback();
            result.errorCode = QStringLiteral("PCV_OUTPUT_002");
            result.message = QStringLiteral("无法暂存已有正式输出文件");
            return result;
        }
        file.hadOriginal = true;
    }
    for (auto &file : files) {
        if (!QFile::rename(file.staged, file.final)) {
            rollback();
            result.errorCode = QStringLiteral("PCV_OUTPUT_002");
            result.message = QStringLiteral("正式输出文件提交失败");
            return result;
        }
        file.committed = true;
    }
    for (auto &file : files) {
        if (file.hadOriginal) QFile::remove(file.backup);
    }
    result.success = true; result.planePng = pngRelative; result.planeJson = jsonRelative;
    result.planeRobotBasePly = plyRelative; result.exportedPointCount = count;
    return result;
}

} // namespace pcv::output
