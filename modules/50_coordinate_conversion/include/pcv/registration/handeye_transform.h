#pragma once

#include <pcv/core/point_types.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

// Module 50 is the single coordinate-conversion implementation of this
// repository. Every caller (scanning JSON, plain PLY, multi-frame stitching,
// temporary workpiece and regression tooling) must reuse the functions
// declared here instead of re-deriving matrices, poses or scan ratios.
//
// Fixed contract:
//   * controller pose order [X,Y,Z,A,B,C] with A=Rx, B=Ry, C=Rz (mm/deg),
//   * rotation R = Rz(C) * Ry(B) * Rx(A),
//   * RTmatDepth2robot is T_flange_depth (flange_from_depth),
//   * p_robot_base = T_base_flange * T_flange_depth * p_camera; the chain is
//     never inverted by default and never patched with a Z flip, mirror or
//     180 deg rotation,
//   * DepthPointLayout::LineProfileXz feeds the rigid transform with [X,0,Z]:
//     the input Y only carries the scan ratio input.y / signedTravel,
//   * DepthPointLayout::FullXyz keeps every camera coordinate,
//   * a point and its normal share the same rigid rotation; the normal is
//     never translated.

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
    // T_flange_depth parsed from RTmatDepth2robot. Never invert it by default.
    QMatrix4x4 flangeFromDepth;
    QString sourceFile;
    bool valid = false;
};

enum class DepthPointLayout {
    FullXyz,
    LineProfileXz
};

// Signed Start -> End motion. A negative signedTravel is a valid reverse scan,
// so it must never be treated as an absolute value.
struct ScanTravelInfo {
    int dominantAxis = -1;
    float signedTravel = 0.0f;
    bool valid = false;
};

// Stable, machine-readable failure classes. Callers and UI report these
// instead of matching localized error text.
enum class RobotCloudStatus {
    Ok = 0,
    InvalidCalibration,
    InvalidStartPose,
    InvalidEndPose,
    NoScanTravel,
    EmptyInput,
    ProgressOutOfRange,
    LineProfileContractViolation,
    Cancelled
};

// Stable ASCII code, e.g. "PCV_M50_CALIBRATION_INVALID".
QString robotCloudStatusCode(RobotCloudStatus status);

// Localized (zh-CN) human-readable description of the status class.
QString robotCloudStatusText(RobotCloudStatus status);

struct CloudTransformOptions {
    DepthPointLayout layout = DepthPointLayout::LineProfileXz;
    int sampleStride = 8;
    bool interpolateRotation = true;
    // Accepted scan-ratio window for LineProfileXz.
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
    RobotCloudStatus status = RobotCloudStatus::Ok;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

// Rejects non-finite values, a bottom row other than [0,0,0,1], non-unit or
// non-orthogonal rotation columns and a determinant other than +1.
bool validateRigidTransform(const QMatrix4x4 &matrix, QString *error = nullptr);

bool loadHandEyeCalibration(const QString &xmlPath,
                            HandEyeCalibration *calibration,
                            QString *error = nullptr);

QMatrix4x4 robotPoseToMatrix(const RobotPose &pose);
RobotPose matrixToRobotPose(const QMatrix4x4 &matrix);

QMatrix4x4 interpolateRobotTransform(const QMatrix4x4 &start,
                                     const QMatrix4x4 &end,
                                     float ratio);

// Start/End translation difference expressed in robot_base. The dominant axis
// defines the scan direction; signedTravel stays signed.
ScanTravelInfo computeScanTravel(const QMatrix4x4 &startBaseFromFlange,
                                 const QMatrix4x4 &endBaseFromFlange);

// T_base_depth = T_base_flange * T_flange_depth. The only place where the
// hand-eye chain is composed; callers must not multiply the matrices by hand.
QMatrix4x4 composeBaseFromDepth(const QMatrix4x4 &baseFromFlange,
                                const HandEyeCalibration &calibration);

// LineProfileXz contract: the camera point entering the rigid transform is
// [x, 0, z]; the input y is the scan-progress coordinate only. The normal is
// copied unchanged.
Point3D lineProfileCameraPoint(const Point3D &input);

// LineProfileXz contract: ratio = inputY / signedTravel. A zero or non-finite
// travel yields 0.0.
float lineProfileScanRatio(float inputY, float signedTravel);

// Applies T_base_flange * T_flange_depth to one camera point. The normal uses
// the same rotation without the translation. Fails on an invalid matrix,
// non-finite input or non-finite result; the point is returned unchanged then.
Point3D transformPointToRobotBase(const Point3D &depthPoint,
                                  const QMatrix4x4 &baseFromFlange,
                                  const HandEyeCalibration &calibration,
                                  QString *error = nullptr);

// Converts a complete scan into robot_base. This is the reference entry point
// used by every scanning JSON / PLY / multi-frame path.
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
