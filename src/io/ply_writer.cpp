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
    if (!options.comment.isEmpty()) header << "comment " << options.comment << "\n";
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
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (const auto &point : points) {
        data << point.x << point.y << point.z;
        if (options.includeNormals) data << point.nx << point.ny << point.nz;
    }
    if (data.status() != QDataStream::Ok || !file.commit()) {
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
