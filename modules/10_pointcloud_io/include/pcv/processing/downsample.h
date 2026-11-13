#pragma once

#include <pcv/core/point_types.h>

#include <QVector>

namespace pcv::detail::filtering {

enum class VoxelRepresentative {
    FirstInputPoint,
    Centroid
};

struct VoxelDownsampleResult {
    QVector<pointcloud::Point3D> points;
    QVector<qsizetype> sourceIndices;
};

QVector<pointcloud::Point3D> proportionalDownsample(
    const QVector<pointcloud::Point3D> &points, int denominator);

VoxelDownsampleResult voxelDownsample(
    const QVector<pointcloud::Point3D> &points, float voxelSize,
    VoxelRepresentative representative = VoxelRepresentative::FirstInputPoint,
    bool discardInvalid = true);

} // namespace pcv::detail::filtering
