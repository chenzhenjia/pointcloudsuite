#pragma once

#include <pcv/camera/frame_data.h>
#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>

namespace pcv::camera {

// Options for converting a point-cloud frame into the project point type.
struct PointCloudConvertOptions {
    // Skip points with any non-finite coordinate (NaN/Inf). The SDK may emit
    // empty points when SDKEmptyPoint is enabled; they typically decode to
    // zero or NaN coordinates and must not reach the renderer.
    bool skipNonFinite = true;
    // Skip points whose three coordinates are exactly zero (a common empty
    // point encoding). Disabled by default to preserve genuine origin points.
    bool skipZero = false;
};

struct PointCloudConvertResult {
    QVector<pointcloud::Point3D> points;
    qsizetype totalSamples = 0;
    qsizetype skipped = 0;
    bool ok = false;
    QString error;
};

// Converts an ImageType_PointCloud (ABC32f) frame into QVector<Point3D>.
// Rejects non-point-cloud frame types and malformed payload lengths. This is
// a pure function and the unit-testable core of the live pipeline.
PointCloudConvertResult convertPointCloudToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options = {});

// Converts an ImageType_Depth (C16) frame into QVector<Point3D> using the
// frame scale/offset fields: x = col*xScale + xOffset, y = row*yScale +
// yOffset, z = depth*zScale + zOffset. This is the offline/mock fallback; the
// live SDK path prefers MV3D_LP_MapDepthToPointCloud.
PointCloudConvertResult convertDepthToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options = {});

// Converts an ImageType_Profile (ABC16) frame into QVector<Point3D> using
// scale/offset per axis. Offline/mock fallback; the live SDK path prefers
// MV3D_LP_ImageConvert (Profile -> PointCloud).
PointCloudConvertResult convertProfileToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options = {});

} // namespace pcv::camera
