#pragma once

#include <pcv/core/point_types.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

namespace pointcloud {

struct RobotPose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
};

struct HandEyeCalibration {
    QMatrix4x4 flangeFromDepth;
    QString sourceFile;
    bool valid = false;
};

enum class DepthPointLayout {
    FullXyz,
    LineProfileXz
};

struct CloudTransformOptions {
    DepthPointLayout layout = DepthPointLayout::LineProfileXz;
    int sampleStride = 8;
    bool interpolateRotation = true;
    float progressMinimum = -0.02f;
    float progressMaximum = 1.02f;
    std::function<bool()> isCancelled;
};

struct RobotCloudResult {
    QVector<Point3D> points;
    QVector<Point3D> samplePoints;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    qsizetype rejectedInvalid = 0;
    qsizetype rejectedRange = 0;
    float inputYMinimum = 0.0f;
    float inputYMaximum = 0.0f;
    float signedTravel = 0.0f;
    int dominantTravelAxis = -1;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

bool validateRigidTransform(const QMatrix4x4 &matrix, QString *error = nullptr);

bool loadHandEyeCalibration(const QString &xmlPath,
                            HandEyeCalibration *calibration,
                            QString *error = nullptr);

QMatrix4x4 robotPoseToMatrix(const RobotPose &pose);
RobotPose matrixToRobotPose(const QMatrix4x4 &matrix);

QMatrix4x4 interpolateRobotTransform(const QMatrix4x4 &start,
                                     const QMatrix4x4 &end,
                                     float ratio);

Point3D transformPointToRobotBase(const Point3D &depthPoint,
                                  const QMatrix4x4 &baseFromFlange,
                                  const HandEyeCalibration &calibration);

RobotCloudResult transformLineScanToRobotBase(
    const QVector<Point3D> &source,
    const HandEyeCalibration &calibration,
    const QMatrix4x4 &startBaseFromFlange,
    const QMatrix4x4 &endBaseFromFlange,
    const CloudTransformOptions &options = {});

RobotCloudResult transformLineScanToRobotBase(
    const QVector<Point3D> &source,
    const HandEyeCalibration &calibration,
    const RobotPose &startPose,
    const RobotPose &endPose,
    const CloudTransformOptions &options = {});

} // namespace pointcloud
