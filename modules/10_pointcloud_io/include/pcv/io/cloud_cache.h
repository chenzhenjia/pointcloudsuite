#pragma once

#include <pcv/io/ply_reader.h>

#include <QString>
#include <QVector>
#include <QStringList>

namespace pcv::detail::io {

struct CachedCloudResult {
    QVector<pointcloud::Point3D> points;
    QString error;
    bool usedCache = false;
    bool cancelled = false;
    bool ok = false;
    QStringList warnings;
    QString canonicalPlyPath;
    bool canonicalReady = false;
    PlyFormat sourceFormat = PlyFormat::Ascii;
    PlyFormat format = PlyFormat::Ascii;
    qsizetype declaredPointCount = 0;
    pointcloud::Point3D minimum;
    pointcloud::Point3D maximum;
    bool hasBounds = false;
    qint64 headerElapsedMs = 0;
    qint64 boundaryScanElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 totalElapsedMs = 0;
    qint64 fingerprintMs = 0;
    qint64 cacheReadMs = 0;
    qint64 cacheWriteMs = 0;
};

struct CloudCachePolicy {
    bool prepareCanonicalPly = true; // Existing callers retain canonical verification.
};

CachedCloudResult readPlyCached(const QString &sourceFile,
                                const QString &cacheDirectory = {},
                                const PlyReadOptions &options = {});
CachedCloudResult readPlyCached(const QString &sourceFile,
                                const QString &cacheDirectory,
                                const PlyReadOptions &options,
                                const CloudCachePolicy &policy);
QString cacheFilePath(const QString &sourceFile,
                      const QString &cacheDirectory = {});

} // namespace pcv::detail::io
