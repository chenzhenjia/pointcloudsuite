#pragma once

#include <pcv/io/ply_reader.h>

#include <QString>
#include <QVector>

namespace pcv::detail::io {

struct CachedCloudResult {
    QVector<pointcloud::Point3D> points;
    QString error;
    bool usedCache = false;
    bool cancelled = false;
    bool ok = false;
    PlyFormat format = PlyFormat::Ascii;
    qsizetype declaredPointCount = 0;
    pointcloud::Point3D minimum;
    pointcloud::Point3D maximum;
    bool hasBounds = false;
    qint64 headerElapsedMs = 0;
    qint64 boundaryScanElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

CachedCloudResult readPlyCached(const QString &sourceFile,
                                const QString &cacheDirectory = {},
                                const PlyReadOptions &options = {});
QString cacheFilePath(const QString &sourceFile,
                      const QString &cacheDirectory = {});

} // namespace pcv::detail::io
