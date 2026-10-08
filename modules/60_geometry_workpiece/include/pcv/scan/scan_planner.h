#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/workpiece/temp_workpiece_geometry.h>

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <functional>

namespace pcv::scan {

constexpr int kMaxInoRobotPoints = 9990;

enum class ScanPlanningMode : quint8 {
    Global,
    LocalRoi
};

enum class ScanTravelDirection : quint8 {
    NegativeYToPositiveY,
    PositiveYToNegativeY
};

struct ScanPlanningOptions {
    ScanPlanningMode mode = ScanPlanningMode::LocalRoi;
    QVector<pointcloud::Point3D> points;
    QVector<int> globalPlaneIndices;
    QVector<int> localRoiIndices;
    pcv::roi::WorkpieceCoordinateSystem workpieceCoordinateSystem;
    ScanTravelDirection travelDirection = ScanTravelDirection::PositiveYToNegativeY;
    float scanHeightMm = 50.0f;
    float cameraWidthMm = 100.0f;
    float overlapMm = 50.0f;
    float travelExtensionMm = 50.0f;
    std::function<bool()> isCancelled;
};

struct ScanTrajectoryPoint {
    int pointId = -1;
    pointcloud::RobotPose robotPose;
    int scanLineIndex = -1;
    bool lineEndpointStart = false;
};

struct ScanTrajectorySegment {
    int scanLineIndex = -1;
    int startPointId = -1;
    int endPointId = -1;
};

struct ScanTrajectoryResult {
    bool ok = false;
    bool cancelled = false;
    QString errorCode;
    QString error;
    ScanPlanningMode mode = ScanPlanningMode::Global;
    ScanTravelDirection travelDirection = ScanTravelDirection::PositiveYToNegativeY;
    pointcloud::RobotPose centerPose;
    QVector3D travelAxisInRobotBase;
    QVector3D transverseAxisInRobotBase;
    float requiredWidthMm = 0.0f;
    float requiredTravelLengthMm = 0.0f;
    float travelExtensionMm = 0.0f;
    float finalTravelLengthMm = 0.0f;
    float cameraWidthMm = 0.0f;
    float singleLineWidthToleranceMm = 0.0f;
    float requestedOverlapMm = 0.0f;
    float actualOverlapMm = 0.0f;
    float lineSpacingMm = 0.0f;
    int scanLineCount = 0;
    QVector<ScanTrajectoryPoint> points;
    QVector<ScanTrajectorySegment> segments;
};

ScanTrajectoryResult planScanTrajectory(const ScanPlanningOptions &options);
QJsonObject scanTrajectoryToJson(const ScanTrajectoryResult &result);
QJsonObject scanTrajectoryToInoRobotJson(
    const ScanTrajectoryResult &result,
    const pcv::roi::WorkpieceCoordinateSystem &workpieceCoordinateSystem);

} // namespace pcv::scan
