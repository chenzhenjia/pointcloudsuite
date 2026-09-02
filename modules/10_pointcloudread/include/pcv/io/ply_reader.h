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
    // Same semantics as asciiWorkerCount but for the binary fast path
    // (fixed-stride mmap + parallel decode).
    int binaryWorkerCount = 0;
    // When true and the file declares an "intensity" vertex property, its
    // value is decoded into PlyReadResult::intensity for every point.
    bool readIntensity = false;
};

struct PlyReadResult {
    QVector<pointcloud::Point3D> points;
    // Optional intensity channel. Populated only when PlyReadOptions::
    // readIntensity was set and the file declares an "intensity" vertex
    // property; otherwise it stays empty. Same size as points when present.
    QVector<float> intensity;
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
    // Actual worker count used by the binary fast path (0 when the generic
    // fallback path was taken).
    int binaryWorkerCount = 0;
    // True when the binary payload was decoded through the parallel
    // fixed-stride path (mmap or owned buffer); false for the serial
    // fallback (list/unsupported layouts).
    bool fastPath = false;
};

PlyReadResult readPly(const QString &fileName, const PlyReadOptions &options = {});

} // namespace pcv::detail::io
