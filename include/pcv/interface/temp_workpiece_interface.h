#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/handeye_transform.h>

#include <QImage>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

namespace pcv::interface {

inline constexpr const char *kTempScanningSchema =
    "sr2026-temp-scanning-info-mvp-2";
inline constexpr const char *kTempScanningKind =
    "single_frame_scanning_info";
inline constexpr const char *kTempWorkpieceSchema =
    "sr2026-temp-workpiece-info-mvp-2";
inline constexpr const char *kTempWorkpieceKind =
    "single_frame_workpiece_roi";
inline constexpr const char *kTempPlaneName = "WObj1";
inline constexpr const char *kErrorInputMissing = "PCV_INPUT_001";
inline constexpr const char *kErrorInputUnsupported = "PCV_INPUT_002";
inline constexpr const char *kErrorTransformInvalid = "PCV_TRANSFORM_001";
inline constexpr const char *kErrorPoseInvalid = "PCV_TRANSFORM_002";
inline constexpr const char *kErrorPlane = "PCV_PLANE_001";
inline constexpr const char *kErrorFrame = "PCV_FRAME_001";
inline constexpr const char *kErrorImage = "PCV_IMAGE_001";
inline constexpr const char *kErrorOutputDirectory = "PCV_OUTPUT_001";
inline constexpr const char *kErrorOutputIncomplete = "PCV_OUTPUT_002";
inline constexpr const char *kErrorContract = "PCV_CONTRACT_001";

struct TempScanningInfo {
    QString schemaVersion;
    QString kind;
    QString createdAtIso8601;
    QString scanId;
    QString pointCloudFile;
    pointcloud::DepthPointLayout pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
    QString layoutWarning;
    QString warning;
    QString coordinateFrame;
    pointcloud::RobotPose robotPoseStart;
    pointcloud::RobotPose robotPoseEnd;
    QString calibrationFile;
    QString calibrationSourceFrame;
    QString calibrationTargetFrame;
    QVector<int> planeSeedIndices;
    bool valid = false;
};

struct TempWorkpieceOptions {
    QString runtimeRoot;
    QString jobId;
    QString scanningInfoPath;
    // Optional UI-selected calibration override. It never modifies the input JSON.
    QString calibrationOverridePath;
    QString outputDirectory;
};

struct TempWorkpiecePreparation {
    bool success = false;
    QString errorCode;
    QString message;
    QString runtimeRoot;
    QString jobId;
    QString interfaceDirectory;
    QString scanningInfoPath;
    QString outputDirectory;
    QString tempWorkpieceInfoPath;
    QString baselineRobotBasePly;
    QString roiTemplateRobotBasePly;
    QString planeMaskPng;
    QString scanId;
    QString schemaVersion;
    QString kind;
    QString sourceFrame;
    QString targetFrame;
    QString coordinateFrame;
    QString createdAtIso8601;
    QString calibrationSourceFrame;
    QString calibrationTargetFrame;
    pointcloud::RobotPose robotPoseStart;
    pointcloud::RobotPose robotPoseEnd;
    QString warning;
    QString resolvedPointCloudFile;
    QString resolvedCalibrationFile;
    pointcloud::DepthPointLayout pointCloudLayout = pointcloud::DepthPointLayout::LineProfileXz;
    qsizetype declaredPointCount = 0;
    qsizetype convertedPointCount = 0;
    qsizetype rejectedInvalidPointCount = 0;
    qsizetype rejectedRangePointCount = 0;
    QVector<pointcloud::Point3D> robotBasePoints;
    QVector<qsizetype> sourceIndices;
};

struct TempWorkpieceFinalizeOptions {
    QString outputDirectory;
    bool allowOverwrite = false;
    QVector<int> planeIndices;
    QVector<int> roiIndices;
    QImage planeMask;
    QVector4D planeModel;
    double rmsErrorMm = 0.0;
    double planeDistanceToleranceMm = 0.4;
    QVector3D originInRobotBase;
    QVector3D axisXInRobotBase{1.0f, 0.0f, 0.0f};
    QVector3D axisYInRobotBase{0.0f, 1.0f, 0.0f};
    QVector3D axisZInRobotBase{0.0f, 0.0f, 1.0f};
    // Controller-style WObj1 angles: A/B/C for
    // R = Rz(A) * Ry(B) * Rx(C).
    QVector3D abcDeg;
    QMatrix4x4 TBaseWorkpiece;
    QMatrix4x4 TWorkpieceBase;
};

struct TempWorkpieceResult {
    bool success = false;
    QString errorCode;
    QString message;
    QString outputDirectory;
    QString tempWorkpieceInfoPath;
    QString baselineRobotBasePly;
    QString roiTemplateRobotBasePly;
    QString planeMaskPng;
    QString createdAtIso8601;
    qsizetype planePointCount = 0;
    qsizetype roiPointCount = 0;
    int imageWidthPx = 0;
    int imageHeightPx = 0;
    double imageWidthMm = 0.0;
    double imageHeightMm = 0.0;
    double pixelSizeMm = 0.05;
};

QString defaultRuntimeRoot();
bool parseTempScanningInfo(const QString &filePath,
                           TempScanningInfo *info,
                           QString *error = nullptr);
TempWorkpiecePreparation prepareTempWorkpiece(const TempWorkpieceOptions &options,
                                              QString *error = nullptr);
TempWorkpieceResult finalizeTempWorkpiece(const TempWorkpiecePreparation &prepared,
                                          const TempWorkpieceFinalizeOptions &options,
                                          QString *error = nullptr);

} // namespace pcv::interface
