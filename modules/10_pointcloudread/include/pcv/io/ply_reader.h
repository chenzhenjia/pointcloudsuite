#pragma once

#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>

#include <functional>

namespace pcv::detail::io {

enum class PlyFormat {
    Ascii,
    BinaryLittleEndian,
    BinaryBigEndian
};

struct PlyReadOptions {
    std::function<void(qsizetype loaded, qsizetype total)> progress;
    std::function<bool()> isCancelled;
    // 0 selects an adaptive count. Positive values are intended for
    // diagnostics and are clamped to the available point count.
    int asciiWorkerCount = 0;
};

struct PlyReadResult {
    QVector<pointcloud::Point3D> points;
    QString error;
    qsizetype declaredPointCount = 0;
    PlyFormat format = PlyFormat::Ascii;
    bool cancelled = false;
    bool ok = false;
    pointcloud::Point3D minimum;
    pointcloud::Point3D maximum;
    bool hasBounds = false;
    qint64 headerElapsedMs = 0;
    qint64 boundaryScanElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 totalElapsedMs = 0;
    int asciiWorkerCount = 0;
};

PlyReadResult readPly(const QString &fileName, const PlyReadOptions &options = {});

} // namespace pcv::detail::io
