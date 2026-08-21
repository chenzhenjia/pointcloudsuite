#include <pcv/output/plane_output.h>

#include <pcv/infrastructure/runtime_paths.h>

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace {

bool invalidComponent(const QString &value)
{
    const QString text = value.trimmed();
    return text.isEmpty() || text == QStringLiteral(".") || text == QStringLiteral("..")
        || text.contains(QStringLiteral("..")) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || QFileInfo(text).isAbsolute();
}

QJsonObject vector3(const QVector3D &value)
{
    return {{QStringLiteral("x"), value.x()}, {QStringLiteral("y"), value.y()},
            {QStringLiteral("z"), value.z()}};
}

QJsonArray matrix4(const QMatrix4x4 &matrix)
{
    QJsonArray result;
    for (int row = 0; row < 4; ++row) {
        QJsonArray line;
        for (int column = 0; column < 4; ++column) line.append(matrix(row, column));
        result.append(line);
    }
    return result;
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
    if (planeIndices.isEmpty() || points.isEmpty()) {
        result.errorCode = QStringLiteral("PCV_PLANE_001"); result.message = QStringLiteral("平面点不足或拟合失败"); return result;
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
    QString error;
    if (!writeImage(directory.filePath(pngRelative), image, &error)) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = error; return result;
    }
    qsizetype count = 0;
    if (!writePly(directory.filePath(plyRelative), points, planeIndices, metadata, &count, &error)) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = error; return result;
    }
    QMatrix4x4 base = metadata.TBaseWorkpiece;
    QMatrix4x4 inverse = metadata.TWorkpieceBase;
    if (base.isIdentity() && !inverse.isIdentity()) base = inverse.inverted();
    if (inverse.isIdentity() && !base.isIdentity()) inverse = base.inverted();
    if (std::abs(base.determinant()) < 1.0e-8f || std::abs(inverse.determinant()) < 1.0e-8f) {
        result.errorCode = QStringLiteral("PCV_FRAME_001");
        result.message = QStringLiteral("工件坐标系矩阵不可逆");
        return result;
    }
    QJsonObject imageObject{{QStringLiteral("file"), pngRelative}, {QStringLiteral("width_px"), image.width()},
        {QStringLiteral("height_px"), image.height()}, {QStringLiteral("pixel_size_mm"), 0.05},
        {QStringLiteral("origin"), QStringLiteral("top_left")}, {QStringLiteral("u_axis"), QStringLiteral("workpiece_x_positive")},
        {QStringLiteral("v_axis"), QStringLiteral("workpiece_y_negative")}, {QStringLiteral("background_value"), 0},
        {QStringLiteral("foreground_value"), 255}};
    QJsonObject rootObject{
        {QStringLiteral("schema"), QString::fromLatin1(kPlaneOutputSchema)},
        {QStringLiteral("job_id"), context.jobId}, {QStringLiteral("workpiece_id"), context.workpieceId},
        {QStringLiteral("source_point_cloud"), metadata.sourcePointCloud},
        {QStringLiteral("units"), QJsonObject{{QStringLiteral("length"), QStringLiteral("mm")}, {QStringLiteral("angle"), QStringLiteral("deg")}}},
        {QStringLiteral("coordinate_frames"), QJsonObject{{QStringLiteral("source"), QStringLiteral("robot_base")}, {QStringLiteral("workpiece"), QStringLiteral("workpiece")}}},
        {QStringLiteral("image"), imageObject},
        {QStringLiteral("workpiece_coordinate"), QJsonObject{
            {QStringLiteral("origin_in_robot_base"), vector3(metadata.originInRobotBase)},
            {QStringLiteral("x_axis"), vector3(metadata.axisXInRobotBase)}, {QStringLiteral("y_axis"), vector3(metadata.axisYInRobotBase)},
            {QStringLiteral("z_axis"), vector3(metadata.axisZInRobotBase)},
            {QStringLiteral("abc_deg"), QJsonObject{{QStringLiteral("a"), metadata.abcDeg.x()}, {QStringLiteral("b"), metadata.abcDeg.y()}, {QStringLiteral("c"), metadata.abcDeg.z()}}},
            {QStringLiteral("T_base_workpiece"), matrix4(base)}, {QStringLiteral("T_workpiece_base"), matrix4(inverse)}}},
        {QStringLiteral("plane"), QJsonObject{{QStringLiteral("equation"), QJsonObject{{QStringLiteral("a"), metadata.planeEquation.x()}, {QStringLiteral("b"), metadata.planeEquation.y()}, {QStringLiteral("c"), metadata.planeEquation.z()}, {QStringLiteral("d"), metadata.planeEquation.w()}}}, {QStringLiteral("rms_error_mm"), metadata.rmsErrorMm}, {QStringLiteral("distance_tolerance_mm"), metadata.distanceToleranceMm}}},
        {QStringLiteral("outputs"), QJsonObject{{QStringLiteral("plane_png"), pngRelative}, {QStringLiteral("plane_json"), jsonRelative}, {QStringLiteral("plane_robot_base_ply"), plyRelative}}},
        {QStringLiteral("diagnostics"), metadata.diagnostics},
        {QStringLiteral("status"), QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("error_code"), QString()}, {QStringLiteral("message"), QString()}}}
    };
    const QByteArray bytes = QJsonDocument(rootObject).toJson(QJsonDocument::Indented);
    QSaveFile json(directory.filePath(jsonRelative));
    if (!json.open(QIODevice::WriteOnly) || json.write(bytes) != bytes.size() || !json.commit()) {
        result.errorCode = QStringLiteral("PCV_OUTPUT_002"); result.message = QStringLiteral("JSON 写入不完整"); return result;
    }
    result.success = true; result.planePng = pngRelative; result.planeJson = jsonRelative;
    result.planeRobotBasePly = plyRelative; result.exportedPointCount = count;
    return result;
}

} // namespace pcv::output
