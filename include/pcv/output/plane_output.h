#pragma once

#include <pcv/core/point_types.h>

#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

namespace pcv::output {

inline constexpr const char *kPlaneOutputSchema =
    "sr2026-temp-workpiece-info-mvp-2";
inline constexpr const char *kErrorInputMissing = "PCV_INPUT_001";
inline constexpr const char *kErrorInputUnsupported = "PCV_INPUT_002";
inline constexpr const char *kErrorTransformInvalid = "PCV_TRANSFORM_001";
inline constexpr const char *kErrorPoseInvalid = "PCV_TRANSFORM_002";
inline constexpr const char *kErrorStitchRejected = "PCV_STITCH_001";
inline constexpr const char *kErrorPlane = "PCV_PLANE_001";
inline constexpr const char *kErrorFrame = "PCV_FRAME_001";
inline constexpr const char *kErrorImage = "PCV_IMAGE_001";
inline constexpr const char *kErrorOutputDirectory = "PCV_OUTPUT_001";
inline constexpr const char *kErrorOutputIncomplete = "PCV_OUTPUT_002";
inline constexpr const char *kErrorContract = "PCV_CONTRACT_001";

struct JobContext {
    QString runtimeRoot;
    QString jobId;
    QString workpieceId;
    QString baseName;
    // Optional direct destination directory selected by the user. When empty,
    // output keeps using runtimeRoot/jobs/<jobId> for the contract path.
    QString destinationDirectory;
};

struct PlaneOutputMetadata {
    QString sourcePointCloud;
    QString sourcePlyEncoding = QStringLiteral("unknown");
    QVector3D originInRobotBase;
    QVector3D axisXInRobotBase{1.0f, 0.0f, 0.0f};
    QVector3D axisYInRobotBase{0.0f, 1.0f, 0.0f};
    QVector3D axisZInRobotBase{0.0f, 0.0f, 1.0f};
    QVector3D abcDeg;
    QMatrix4x4 TBaseWorkpiece;
    QMatrix4x4 TWorkpieceBase;
    QVector4D planeEquation{0.0f, 0.0f, 1.0f, 0.0f};
    double rmsErrorMm = 0.0;
    double distanceToleranceMm = 0.4;
    double physicalWidthMm = 0.0;
    double physicalHeightMm = 0.0;
    double pixelSizeMm = 0.05;
    double marginMm = 0.0;
    double roundIncrementMm = 10.0;
    bool automaticBounds = false;
    bool edgeMask = false;
    QJsonObject diagnostics;
};

struct PlaneOutputResult {
    bool success = false;
    QString errorCode;
    QString message;
    QString planePng;
    QString planeJson;
    QString planeRobotBasePly;
    qsizetype exportedPointCount = 0;
};

QString defaultRuntimeRoot();
bool validateJobContext(const JobContext &context, QString *error = nullptr);
QString jobRoot(const JobContext &context);

PlaneOutputResult writePlaneOutput(const JobContext &context,
                                   const QImage &image,
                                   const QVector<pointcloud::Point3D> &points,
                                   const QVector<int> &planeIndices,
                                   const PlaneOutputMetadata &metadata = {});

} // namespace pcv::output
