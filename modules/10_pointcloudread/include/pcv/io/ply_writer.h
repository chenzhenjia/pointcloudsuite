#pragma once

#include <pcv/core/point_types.h>
#include <pcv/io/ply_reader.h>

#include <QString>
#include <QVector>

namespace pcv::detail::io {

struct BinaryPlyWriteOptions {
    bool includeNormals = true;
    QString comment;
};

bool writeBinaryPly(const QString &fileName,
                    const QVector<pointcloud::Point3D> &points,
                    QString *error = nullptr,
                    const BinaryPlyWriteOptions &options = {});

QString canonicalPlyFilePath(const QString &sourceFile,
                             const QString &cacheDirectory = {});

struct CanonicalPlyResult {
    QString path;
    QString error;
    PlyFormat sourceFormat = PlyFormat::Ascii;
    bool converted = false;
    bool cancelled = false;
    bool ok = false;
};

CanonicalPlyResult ensureCanonicalBinaryPly(const QString &sourceFile,
                                            const QString &cacheDirectory = {},
                                            const PlyReadOptions &options = {});

} // namespace pcv::detail::io
