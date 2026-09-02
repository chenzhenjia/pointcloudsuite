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
    const QString path = cacheFilePathForIdentity(source, sourceFile, cacheDirectory);
    if (readCache(source, path, &result.points)) {
        result.usedCache = true;
        result.ok = true;
        result.declaredPointCount = result.points.size();
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
    const QString canonical = canonicalPlyFilePath(sourceFile, cacheDirectory);
    BinaryPlyWriteOptions writeOptions;
    writeOptions.includeNormals = true;
    writeOptions.comment = QStringLiteral("canonical source_format %1")
        .arg(parsed.format == PlyFormat::Ascii ? QStringLiteral("ascii")
             : parsed.format == PlyFormat::BinaryLittleEndian ? QStringLiteral("binary_little_endian")
             : QStringLiteral("binary_big_endian"));
    // The canonical PLY is an audit artifact with no production consumer;
    // a failure to write it must not discard the successfully read cloud.
    QString canonicalError;
    if (!writeBinaryPly(canonical, result.points, &canonicalError, writeOptions)) {
        result.warnings.push_back(QStringLiteral("无法写入 canonical PLY: %1").arg(canonicalError));
        qWarning() << "无法写入 canonical PLY:" << canonicalError;
    }
    result.ok = true;
    return result;
}

} // namespace pcv::detail::io
