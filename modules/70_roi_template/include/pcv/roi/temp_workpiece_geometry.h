#pragma once

#include <pcv/core/point_types.h>
#include <QImage>
#include <QMatrix4x4>
#include <QVector>
#include <QVector3D>

#include <functional>

namespace pcv::roi {

struct AxisPointSelection {
    int xPointIndex = -1;
    int yPointIndex = -1;
    float targetDistanceMm = 0.0f;
    float xActualDistanceMm = 0.0f;
    float yActualDistanceMm = 0.0f;
    bool xUsedFallback = false;
    bool yUsedFallback = false;
    QString error;
    bool ok = false;
};

struct WorkpieceCoordinateSystem {
    QVector3D originInRobotBase;
    QVector3D axisXInRobotBase{1.0f, 0.0f, 0.0f};
    QVector3D axisYInRobotBase{0.0f, 1.0f, 0.0f};
    QVector3D axisZInRobotBase{0.0f, 0.0f, 1.0f};
    QMatrix4x4 workpieceToRobotBase;
    QMatrix4x4 robotBaseToWorkpiece;
    float poseA = 0.0f;
    float poseB = 0.0f;
    float poseC = 0.0f;
    float orthogonalityError = 0.0f;
    QString error;
    bool valid = false;
};

struct PlaneMaskResult {
    QImage image;
    QVector<int> roiIndices;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

AxisPointSelection selectAutomaticAxisPoints(
    const QVector<pointcloud::Point3D> &points,
    const QVector<int> &planeIndices,
    const QVector3D &origin,
    const QVector3D &planeNormal,
    float planeToleranceMm = 0.4f);

WorkpieceCoordinateSystem buildWorkpieceCoordinateSystemFromThreePoints(
    const QVector<pointcloud::Point3D> &points,
    const QVector3D &origin,
    int xAxisPointIndex,
    int yAxisPointIndex);

PlaneMaskResult rasterizePlaneMask(
    const QVector<pointcloud::Point3D> &points,
    const QVector<int> &planeIndices,
    const QVector4D &planeModel,
    const WorkpieceCoordinateSystem &frame,
    float widthMm,
    float heightMm,
    float pixelSizeMm = 0.05f,
    const QVector<int> &explicitRoiIndices = {},
    float planeDistanceToleranceMm = 0.4f,
    const std::function<bool()> &isCancelled = {});

} // namespace pcv::roi
