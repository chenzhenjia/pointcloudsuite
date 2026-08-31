#pragma once

#include <pcv/core/point_types.h>

#include <QVector>
#include <QString>
#include <QMatrix4x4>
#include <QVector3D>

namespace pcv::render {

enum class PointState : quint8 { Normal = 0, Plane = 1, Edge = 2 };

// Render-only data transfer objects. These types intentionally contain no
// processor/application dependencies so PointCloudCanvas can move into this
// module without creating an apps -> shared-library cycle.
struct CoordinateFrame {
    QVector3D originInRobotBase;
    QVector3D axisXInRobotBase;
    QVector3D axisYInRobotBase;
    QVector3D axisZInRobotBase;
    QMatrix4x4 workpieceToRobotBase;
    QMatrix4x4 robotBaseToWorkpiece;
    float poseA = 0.0f;
    float poseB = 0.0f;
    float poseC = 0.0f;
    bool valid = false;
};

struct Contour {
    QVector<QVector3D> points;
    bool hole = false;
};

struct RenderSnapshot {
    QVector<pointcloud::Point3D> points;
    QVector<PointState> states;
    quint64 revision = 0;
};

bool validateSnapshot(const RenderSnapshot &snapshot, QString *error = nullptr);
bool validateCoordinateFrame(const CoordinateFrame &frame, QString *error = nullptr);
bool validateContours(const QVector<Contour> &contours, QString *error = nullptr);

} // namespace pcv::render
