#pragma once

#include <pcv/workpiece/temp_workpiece_geometry.h>

namespace pcv::roi {
// Full measured reference-plane points; no display sampling or registration.
WorkpieceCoordinateSystem buildWorkpieceCoordinateSystemFromRobotAxes(
    const QVector3D &origin, const QVector3D &planeNormal);
}
