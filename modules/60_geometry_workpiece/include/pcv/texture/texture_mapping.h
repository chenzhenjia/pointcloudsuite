#pragma once

#include <pcv/core/point_types.h>

#include <QImage>
#include <QString>
#include <QVector>
#include <functional>

namespace pcv::texture {

struct MappingOptions {
    // Each current point keeps the index of its original PLY raster sample.
    // The mapping is intentionally strict: no current-index fallback is allowed.
    QVector<qsizetype> sourceIndices;
    qsizetype declaredPointCount = 0;
    qsizetype rejectedInvalidPointCount = 0;
    qsizetype rejectedRangePointCount = 0;
    std::function<bool()> isCancelled;
};

struct MappingResult {
    QVector<quint8> grayscale;
    QVector<quint8> valid;
    int imageWidth = 0;
    int imageHeight = 0;
    qsizetype sourcePointCount = 0;
    qsizetype currentPointCount = 0;
    int planePointCount = 0;
    int mappedPointCount = 0;
    int skippedPointCount = 0;
    int conflictPointCount = 0;
    QString mappingMode;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

struct MultiFrameBrightnessInput {
    QVector<pointcloud::Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<QByteArray> frameMono8;
    QVector<int> frameWidths;
    QVector<int> frameHeights;
    std::function<bool()> isCancelled;
};

MappingResult mapPlaneBrightness(const QVector<pointcloud::Point3D> &points,
                                 const QVector<int> &planeIndices,
                                 const QImage &image,
                                 const MappingOptions &options);

// Maps each stitched point back to its source capture raster. The source
// cloud id is also the deterministic frame-order key; callers that fuse
// duplicate target points should apply later cloud ids last.
MappingResult mapMultiFrameBrightness(const MultiFrameBrightnessInput &input);

} // namespace pcv::texture
