#pragma once

#include <pcv/core/point_types.h>

#include <QVector>
#include <QString>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

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
    QVector4D color = QVector4D(1.0f, 0.86f, 0.18f, 1.0f);
};

struct PlanarMaskOverlay {
    // World-space mesh from the complete source fit; ranges describe closed loops.
    QVector<QVector3D> vertices;
    QVector<quint32> triangleIndices;
    QVector<QVector3D> boundaryVertices;
    QVector<QPair<int, int>> boundaryRanges;
    QVector4D color = QVector4D(0.09f, 0.46f, 0.91f, 1.0f);
    float opacity = 0.25f;
};

struct RenderSnapshot {
    QVector<pointcloud::Point3D> points;
    QVector<PointState> states;
    quint64 revision = 0;
};

struct PointColorOverlay {
    QVector<QVector4D> colors;
    QVector<quint8> valid;
};

struct RenderOverlayOptions {
    bool showCoordinateAxes = true;
    bool showFloorGrid = false;
    bool floorZUp = true;
    bool showAxisLabels = true;
    bool showNormals = false;
    bool showBoundingBox = false;
    bool showStatistics = false;
    bool showColorBar = true;
    bool showOrientation = true;
};

bool validateSnapshot(const RenderSnapshot &snapshot, QString *error = nullptr);
bool validatePointColorOverlay(const PointColorOverlay &overlay, qsizetype pointCount,
                               QString *error = nullptr);
bool validateCoordinateFrame(const CoordinateFrame &frame, QString *error = nullptr);
bool validateContours(const QVector<Contour> &contours, QString *error = nullptr);
bool validatePlanarMaskOverlay(const PlanarMaskOverlay &overlay, QString *error = nullptr);

} // namespace pcv::render
