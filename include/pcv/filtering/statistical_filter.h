#pragma once

#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>

namespace pcv::detail::filtering {

struct StatisticalFilterOptions {
    int meanK = 45;
    float stddevMultiplier = 1.3f;
    float cellSize = 0.0f;
};

struct StatisticalFilterResult {
    QVector<pointcloud::Point3D> points;
    QString warning;
    float usedCellSize = 0.0f;
    float distanceThreshold = 0.0f;
    int measuredPointCount = 0;
};

float estimateCellSize(const QVector<pointcloud::Point3D> &points);
StatisticalFilterResult removeStatisticalOutliers(
    const QVector<pointcloud::Point3D> &points,
    const StatisticalFilterOptions &options = {});

} // namespace pcv::detail::filtering
