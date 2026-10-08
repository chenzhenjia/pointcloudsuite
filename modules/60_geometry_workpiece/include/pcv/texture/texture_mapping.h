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
    QString mappingMode;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

MappingResult mapPlaneBrightness(const QVector<pointcloud::Point3D> &points,
                                 const QVector<int> &planeIndices,
                                 const QImage &image,
                                 const MappingOptions &options);

} // namespace pcv::texture
