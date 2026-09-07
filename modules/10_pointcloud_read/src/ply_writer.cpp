#include <pcv/io/ply_writer.h>

#include <pcv/io/cloud_cache.h>
#include <pcv/infrastructure/runtime_paths.h>

#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QSysInfo>
#include <QDataStream>
#include <cstddef>

#include <limits>
#include <type_traits>

static_assert(sizeof(pointcloud::Point3D) == 6 * sizeof(float),
              "Point3D binary PLY layout must contain six contiguous float32 values");
static_assert(std::is_standard_layout_v<pointcloud::Point3D>,
              "Point3D binary PLY layout must be standard-layout");
static_assert(std::is_trivially_copyable_v<pointcloud::Point3D>,
              "Point3D binary PLY layout must be trivially copyable");
static_assert(offsetof(pointcloud::Point3D, x) == 0
              && offsetof(pointcloud::Point3D, y) == sizeof(float)
              && offsetof(pointcloud::Point3D, z) == 2 * sizeof(float)
              && offsetof(pointcloud::Point3D, nx) == 3 * sizeof(float)
              && offsetof(pointcloud::Point3D, ny) == 4 * sizeof(float)
              && offsetof(pointcloud::Point3D, nz) == 5 * sizeof(float),
              "Point3D fields must be contiguous float32 values");

namespace {
QString canonicalPath(const QString &sourceFile, const QString &cacheDirectory)
{
    QString base = QFileInfo(sourceFile).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    const QString key = QString::fromLatin1(QCryptographicHash::hash(
        QFileInfo(sourceFile).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256)
        .toHex().left(16));
    const QString root = cacheDirectory.isEmpty() ? pcv::runtime::cacheDirectory() : cacheDirectory;
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("canonical_%1_%2.ply").arg(base, key));
}
}

namespace pcv::detail::io {

QString canonicalPlyFilePath(const QString &sourceFile, const QString &cacheDirectory)
{
    return canonicalPath(sourceFile, cacheDirectory);
}

bool writeBinaryPly(const QString &fileName,
                    const QVector<pointcloud::Point3D> &points,
                    QString *error,
                    const BinaryPlyWriteOptions &options)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream header(&file);
    header.setEncoding(QStringConverter::Utf8);
    header << "ply\nformat binary_little_endian 1.0\n";
    header << "comment encoding binary_little_endian\n";
    if (!options.comment.isEmpty()) header << "comment " << options.comment << "\n";
    for (const QString &comment : options.comments) {
        if (!comment.isEmpty()) header << "comment " << comment << "\n";
    }
    header << "element vertex " << points.size() << "\n"
           << "property float x\nproperty float y\nproperty float z\n";
    if (options.includeNormals)
        header << "property float nx\nproperty float ny\nproperty float nz\n";
    header << "end_header\n";
    header.flush();
    if (header.status() != QTextStream::Ok) {
        if (error) *error = QStringLiteral("写入 PLY Header 失败");
        return false;
    }
    // Bulk write: build the interleaved float payload once and write it in
    // one shot instead of streaming per-point through QDataStream.
    if (!points.isEmpty()) {
        if (options.includeNormals && !options.isCancelled
            && QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
            // pointcloud::Point3D layout is x,y,z,nx,ny,nz (6 native floats,
            // little-endian on the supported targets) which matches the
            // declared property order exactly.
            const qsizetype bytes = points.size()
                * qsizetype(sizeof(pointcloud::Point3D));
            if (bytes > std::numeric_limits<qint64>::max()) {
                if (error) *error = QStringLiteral("PLY 顶点数据大小溢出");
                return false;
            }
            if (file.write(reinterpret_cast<const char *>(points.constData()),
                           bytes) != bytes) {
                if (error) *error = file.errorString().isEmpty()
                    ? QStringLiteral("PLY 写入失败") : file.errorString();
                return false;
            }
        } else {
            const qsizetype fields = options.includeNormals ? 6 : 3;
            if (points.size() > std::numeric_limits<qsizetype>::max()
                    / (fields * qsizetype(sizeof(float)))
                || points.size() * fields * qsizetype(sizeof(float))
                    > std::numeric_limits<int>::max()) {
                if (error) *error = QStringLiteral("PLY 顶点数据大小溢出");
                return false;
            }
            const int byteCount = int(points.size() * fields * qsizetype(sizeof(float)));
            QByteArray payload;
            payload.reserve(byteCount);
            QDataStream stream(&payload, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            for (qsizetype index = 0; index < points.size(); ++index) {
                if ((index & 0x3fff) == 0 && options.isCancelled && options.isCancelled()) {
                    file.cancelWriting();
                    if (error) *error = QStringLiteral("已取消");
                    return false;
                }
                const auto &point = points[index];
                stream << point.x << point.y << point.z;
                if (options.includeNormals) stream << point.nx << point.ny << point.nz;
            }
            if (stream.status() != QDataStream::Ok) {
                if (error) *error = QStringLiteral("PLY 顶点数据编码失败");
                return false;
            }
            if (file.write(payload) != byteCount) {
                if (error) *error = file.errorString().isEmpty()
                    ? QStringLiteral("PLY 写入失败") : file.errorString();
                return false;
            }
        }
    }
    if (!file.commit()) {
        if (error) *error = file.errorString().isEmpty()
            ? QStringLiteral("PLY 写入失败") : file.errorString();
        return false;
    }
    return true;
}

CanonicalPlyResult ensureCanonicalBinaryPly(const QString &sourceFile,
                                            const QString &cacheDirectory,
                                            const PlyReadOptions &options)
{
    CanonicalPlyResult result;
    const QString path = canonicalPath(sourceFile, cacheDirectory);
    const PlyReadResult parsed = readPly(sourceFile, options);
    result.sourceFormat = parsed.format;
    result.path = path;
    if (!parsed.ok) {
        result.error = parsed.error;
        result.cancelled = parsed.cancelled;
        return result;
    }
    BinaryPlyWriteOptions writeOptions;
    writeOptions.includeNormals = true;
    writeOptions.comment = QStringLiteral("canonical source_format %1")
        .arg(parsed.format == PlyFormat::Ascii ? QStringLiteral("ascii")
             : parsed.format == PlyFormat::BinaryLittleEndian ? QStringLiteral("binary_little_endian")
             : QStringLiteral("binary_big_endian"));
    if (!writeBinaryPly(path, parsed.points, &result.error, writeOptions)) return result;
    result.converted = true;
    result.ok = true;
    return result;
}

} // namespace pcv::detail::io
