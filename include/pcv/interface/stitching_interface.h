#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/handeye_transform.h>
#include <pcv/registration/multiframe_registration.h>
#include <pcv/registration/seam_fusion.h>

#include <QString>
#include <QVector>

#include <functional>

namespace pcv::interface {

struct StitchingFrameInput {
    QString plyPath;
    pointcloud::RobotPose startPose;
    pointcloud::RobotPose endPose;
};

struct StitchingOptions {
    QString calibrationPath;
    QString outputDirectory;
    QVector<StitchingFrameInput> frames;
    int sampleStride = 8;
    bool seamEnabled = false;
    float seamHalfWidthMm = 8.0f;
    std::function<bool()> isCancelled;
    std::function<void(float, const QString &)> progress;
};

struct StitchingResult {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString message;
    QString outputPly;
    QVector<pointcloud::Point3D> points;
    QVector<pointcloud::IcpDiagnostics> icpDiagnostics;
    QVector<pointcloud::SeamFusionDiagnostic> seamDiagnostics;
    QString diagnostics;
};

StitchingResult stitchRawLineProfiles(const StitchingOptions &options);

} // namespace pcv::interface
