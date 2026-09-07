#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/handeye_transform.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector4D>

#include <functional>

namespace pcv::matching {

// Input produced by the plane/ROI workflow. The source cloud remains in its
// camera/depth coordinates; matching performs the one authoritative conversion
// through module 50 and returns the selected points in robot_base coordinates.
struct RoiMatchInput {
    QVector<pointcloud::Point3D> sourcePoints;
    QVector<int> planeIndices;
    QVector<int> roiIndices;
    QVector4D planeModel;
    pointcloud::HandEyeCalibration calibration;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    pointcloud::CloudTransformOptions transformOptions;
    std::function<bool()> isCancelled;
};

struct RoiMatchResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QVector<pointcloud::Point3D> robotBasePoints;
    QVector<qsizetype> sourceIndices;
    qsizetype inputPointCount = 0;
    qsizetype requestedPointCount = 0;
    qsizetype matchedPointCount = 0;
    qsizetype missingPointCount = 0;
    qsizetype rejectedInvalidPointCount = 0;
    qsizetype rejectedRangePointCount = 0;
    float coverage = 0.0f;
};

RoiMatchResult matchRoiToRobotBase(const RoiMatchInput &input);

} // namespace pcv::matching
