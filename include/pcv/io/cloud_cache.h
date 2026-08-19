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
};

CachedCloudResult readPlyCached(const QString &sourceFile,
                                const QString &cacheDirectory = {},
                                const PlyReadOptions &options = {});
QString cacheFilePath(const QString &sourceFile,
                      const QString &cacheDirectory = {});

} // namespace pcv::detail::io
