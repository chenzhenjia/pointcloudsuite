#include <pcv/io/cloud_cache.h>

#include <pcv/io/ply_reader.h>
#include <pcv/io/ply_writer.h>
#include <pcv/infrastructure/runtime_paths.h>

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <cstring>
#include <limits>

namespace {
constexpr quint32 CacheMagic = 0x31564350; // PCV1
constexpr quint32 CacheVersion = 3;

struct SourceIdentity {
    QString absolutePath;
    qint64 size = 0;
    qint64 modifiedMs = 0;
    QByteArray sha256;
    bool valid = false;
};

SourceIdentity sourceIdentity(const QString &sourceFile) {
    SourceIdentity identity;
    const QFileInfo info(sourceFile);
    identity.absolutePath = info.absoluteFilePath();
    identity.size = info.size();
    identity.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly)) return identity;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd()) return identity;
        hash.addData(chunk);
    }
    identity.sha256 = hash.result();
    identity.valid = true;
    return identity;
}

QString resolvedCacheDirectory(const QString &requested) {
    const QString path = requested.isEmpty() ? pcv::runtime::cacheDirectory() : requested;
    QDir().mkpath(path);
    return QDir::cleanPath(path);
}

QString cacheFilePathForIdentity(const SourceIdentity &source,
                                 const QString &sourceFile,
                                 const QString &cacheDirectory)
{
    QString baseName = QFileInfo(sourceFile).completeBaseName();
    baseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                     QStringLiteral("_"));
    const QByteArray keyInput = source.absolutePath.toUtf8() + source.sha256;
    const QString key = QString::fromLatin1(QCryptographicHash::hash(
        keyInput, QCryptographicHash::Sha256).toHex().left(16));
    return QDir(resolvedCacheDirectory(cacheDirectory)).filePath(
        QStringLiteral("ply_%1_%2.pcvbin").arg(baseName, key));
}

bool readCache(const SourceIdentity &source, const QString &path,
               QVector<pointcloud::Point3D> *points) {
    QFile cache(path);
    if (!cache.open(QIODevice::ReadOnly) || cache.size() < 32) return false;
    if (!source.valid || QFileInfo(path).lastModified().toMSecsSinceEpoch() < source.modifiedMs)
        return false;

    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    quint64 count = 0, sourceSize = 0;
    qint64 sourceStamp = 0;
    QByteArray fingerprint;
    stream >> magic >> version >> count >> sourceSize >> sourceStamp;
    stream >> fingerprint;
    if (stream.status() != QDataStream::Ok || magic != CacheMagic || version != CacheVersion
        || sourceSize != quint64(source.size)
        || sourceStamp != source.modifiedMs
        || fingerprint != source.sha256
        || count > quint64(std::numeric_limits<qsizetype>::max())
        || count > quint64(std::numeric_limits<qint64>::max() / qint64(sizeof(pointcloud::Point3D))))
        return false;

    points->resize(qsizetype(count));
    const qint64 bytes = qint64(count) * qint64(sizeof(pointcloud::Point3D));
    if (cache.read(reinterpret_cast<char *>(points->data()), bytes) != bytes) {
        points->clear();
        return false;
    }
    return true;
}

bool writeCache(const SourceIdentity &source, const QString &path,
                const QVector<pointcloud::Point3D> &points) {
    if (!source.valid) return false;
    if (points.size() > qsizetype(std::numeric_limits<qint64>::max()
                                  / qint64(sizeof(pointcloud::Point3D)))) return false;
    QSaveFile cache(path);
    if (!cache.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << CacheMagic << CacheVersion << quint64(points.size())
           << quint64(source.size) << source.modifiedMs << source.sha256;
    const qint64 bytes = qint64(points.size()) * qint64(sizeof(pointcloud::Point3D));
    if (cache.write(reinterpret_cast<const char *>(points.constData()), bytes) != bytes
        || stream.status() != QDataStream::Ok) return false;
    return cache.commit();
}

bool canonicalReady(const QString &path, const QVector<pointcloud::Point3D> &expectedPoints)
{
    const pcv::detail::io::PlyHeaderInfo header = pcv::detail::io::inspectPlyHeader(path);
    if (!header.ok || header.format != pcv::detail::io::PlyFormat::BinaryLittleEndian
        || header.version != QStringLiteral("1.0")
        || header.declaredPointCount != expectedPoints.size()) {
        return false;
    }
    const pcv::detail::io::PlyReadResult canonical = pcv::detail::io::readPly(path);
    return canonical.ok && canonical.points.size() == expectedPoints.size()
        && (canonical.points.isEmpty() || std::memcmp(canonical.points.constData(),
                                                       expectedPoints.constData(),
                                                       size_t(canonical.points.size())
                                                           * sizeof(pointcloud::Point3D)) == 0);
}
}

namespace pcv::detail::io {

QString cacheFilePath(const QString &sourceFile, const QString &cacheDirectory) {
    const SourceIdentity source = sourceIdentity(sourceFile);
    return cacheFilePathForIdentity(source, sourceFile, cacheDirectory);
}

CachedCloudResult readPlyCached(const QString &sourceFile, const QString &cacheDirectory,
                                const PlyReadOptions &options) {
    CachedCloudResult result;
    if (options.isCancelled && options.isCancelled()) {
        result.cancelled = true;
        result.error = QStringLiteral("PLY reading cancelled");
        return result;
    }
    const SourceIdentity source = sourceIdentity(sourceFile);
    if (!source.valid) {
        result.error = QStringLiteral("无法读取 PLY 源文件指纹");
        return result;
    }
    const PlyHeaderInfo sourceHeader = inspectPlyHeader(sourceFile);
    if (!sourceHeader.ok) {
        result.error = sourceHeader.error;
        return result;
    }
    result.sourceFormat = sourceHeader.format;
    result.format = sourceHeader.format;
    result.canonicalPlyPath = canonicalPlyFilePath(sourceFile, cacheDirectory);
    const QString path = cacheFilePathForIdentity(source, sourceFile, cacheDirectory);
    if (readCache(source, path, &result.points)) {
        result.usedCache = true;
        result.ok = true;
        // Preserve the source PLY header count even when the decoded point
        // payload comes from cache. Strict raster provenance must compare the
        // image pixel count with the original declaration, never with a
        // potentially stale or compressed cache length.
        result.declaredPointCount = sourceHeader.declaredPointCount;
        if (result.declaredPointCount != result.points.size()) {
            result.points.clear();
            result.ok = false;
            result.usedCache = false;
        }
    }
    if (result.ok) {
        if (!canonicalReady(result.canonicalPlyPath, result.points)) {
            BinaryPlyWriteOptions writeOptions;
            writeOptions.includeNormals = true;
            writeOptions.comment = QStringLiteral("canonical source_format %1")
                .arg(result.sourceFormat == PlyFormat::Ascii ? QStringLiteral("ascii")
                     : result.sourceFormat == PlyFormat::BinaryLittleEndian ? QStringLiteral("binary_little_endian")
                     : QStringLiteral("binary_big_endian"));
            QString canonicalError;
            if (!writeBinaryPly(result.canonicalPlyPath, result.points, &canonicalError, writeOptions)
                || !canonicalReady(result.canonicalPlyPath, result.points)) {
                result.error = canonicalError.isEmpty()
                    ? QStringLiteral("canonical Binary PLY 校验失败") : canonicalError;
                result.points.clear();
                result.ok = false;
                return result;
            }
        }
        result.canonicalReady = true;
        if (options.progress) options.progress(result.points.size(), result.points.size());
        return result;
    }
    PlyReadResult parsed = readPly(sourceFile, options);
    if (!parsed.ok) {
        result.error = parsed.error;
        result.cancelled = parsed.cancelled;
        return result;
    }
    result.points.swap(parsed.points);
    result.sourceFormat = parsed.format;
    result.format = parsed.format;
    result.declaredPointCount = parsed.declaredPointCount;
    result.minimum = parsed.minimum;
    result.maximum = parsed.maximum;
    result.hasBounds = parsed.hasBounds;
    result.headerElapsedMs = parsed.headerElapsedMs;
    result.boundaryScanElapsedMs = parsed.boundaryScanElapsedMs;
    result.parseElapsedMs = parsed.parseElapsedMs;
    result.totalElapsedMs = parsed.totalElapsedMs;
    if (!writeCache(source, path, result.points)) {
        result.error = QStringLiteral("无法写入 PLY 点云缓存");
        result.points.clear();
        return result;
    }
    const QString canonical = result.canonicalPlyPath;
    BinaryPlyWriteOptions writeOptions;
    writeOptions.includeNormals = true;
    writeOptions.comment = QStringLiteral("canonical source_format %1")
        .arg(parsed.format == PlyFormat::Ascii ? QStringLiteral("ascii")
             : parsed.format == PlyFormat::BinaryLittleEndian ? QStringLiteral("binary_little_endian")
             : QStringLiteral("binary_big_endian"));
    QString canonicalError;
    if (!writeBinaryPly(canonical, result.points, &canonicalError, writeOptions)
        || !canonicalReady(canonical, result.points)) {
        result.error = canonicalError.isEmpty()
            ? QStringLiteral("canonical Binary PLY 校验失败") : canonicalError;
        result.points.clear();
        result.ok = false;
        return result;
    }
    result.canonicalReady = true;
    result.ok = true;
    return result;
}

} // namespace pcv::detail::io
