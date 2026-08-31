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
constexpr quint32 CacheVersion = 2;

QByteArray sourceFingerprint(const QString &sourceFile) {
    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return hash.result();
}

QString resolvedCacheDirectory(const QString &requested) {
    const QString path = requested.isEmpty() ? pcv::runtime::cacheDirectory() : requested;
    QDir().mkpath(path);
    return QDir::cleanPath(path);
}

bool readCache(const QString &sourceFile, const QString &path,
               QVector<pointcloud::Point3D> *points) {
    QFileInfo sourceInfo(sourceFile);
    sourceInfo.refresh();
    QFile cache(path);
    if (!cache.open(QIODevice::ReadOnly) || cache.size() < 32) return false;
    if (QFileInfo(path).lastModified() < sourceInfo.lastModified()) return false;

    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    quint64 count = 0, sourceSize = 0;
    qint64 sourceStamp = 0;
    QByteArray fingerprint;
    stream >> magic >> version >> count >> sourceSize >> sourceStamp;
    stream >> fingerprint;
    if (stream.status() != QDataStream::Ok || magic != CacheMagic || version != CacheVersion
        || sourceSize != quint64(sourceInfo.size())
        || sourceStamp != sourceInfo.lastModified().toMSecsSinceEpoch()
        || fingerprint != sourceFingerprint(sourceFile)
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

bool writeCache(const QString &sourceFile, const QString &path,
                const QVector<pointcloud::Point3D> &points) {
    const QFileInfo sourceInfo(sourceFile);
    if (points.size() > qsizetype(std::numeric_limits<qint64>::max()
                                  / qint64(sizeof(pointcloud::Point3D)))) return false;
    QSaveFile cache(path);
    if (!cache.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&cache);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << CacheMagic << CacheVersion << quint64(points.size())
           << quint64(sourceInfo.size()) << sourceInfo.lastModified().toMSecsSinceEpoch()
           << sourceFingerprint(sourceFile);
    const qint64 bytes = qint64(points.size()) * qint64(sizeof(pointcloud::Point3D));
    if (cache.write(reinterpret_cast<const char *>(points.constData()), bytes) != bytes
        || stream.status() != QDataStream::Ok) return false;
    return cache.commit();
}
}

namespace pcv::detail::io {

QString cacheFilePath(const QString &sourceFile, const QString &cacheDirectory) {
    QString baseName = QFileInfo(sourceFile).completeBaseName();
    baseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                     QStringLiteral("_"));
    const QByteArray absolutePath = QFileInfo(sourceFile).absoluteFilePath().toUtf8();
    const QByteArray content = sourceFingerprint(sourceFile);
    const QByteArray keyInput = absolutePath + content;
    const QString key = QString::fromLatin1(QCryptographicHash::hash(
        keyInput, QCryptographicHash::Sha256).toHex().left(16));
    return QDir(resolvedCacheDirectory(cacheDirectory)).filePath(
        QStringLiteral("ply_%1_%2.pcvbin").arg(baseName, key));
}

CachedCloudResult readPlyCached(const QString &sourceFile, const QString &cacheDirectory,
                                const PlyReadOptions &options) {
    CachedCloudResult result;
    if (options.isCancelled && options.isCancelled()) {
        result.cancelled = true;
        result.error = QStringLiteral("PLY reading cancelled");
        return result;
    }
    const QString path = cacheFilePath(sourceFile, cacheDirectory);
    if (readCache(sourceFile, path, &result.points)) {
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
    if (!writeCache(sourceFile, path, result.points)) {
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
    if (!writeBinaryPly(canonical, result.points, &result.error, writeOptions)) {
        result.points.clear();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace pcv::detail::io
