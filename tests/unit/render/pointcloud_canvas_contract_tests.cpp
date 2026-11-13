#include <pcv/render/pointcloud_canvas_contract.h>

#include "overlay_geometry.h"

#include <QCoreApplication>

#include <iostream>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pcv::render::CloudProjectionSnapshotV1 projection;
    projection.viewport={200,100};projection.span=2;
    bool front=false,back=false;
    const auto a=projection.project({0,0,-.5f},&front);
    const auto b=projection.project({0,0,.5f},&back);
    if(!front||!back||a!=QPointF(100,50)||a!=b)return 1;
    projection.pan={.5,0};if(projection.project({0,0,0})!=QPointF(150,50))return 1;
    projection.span=0;projection.project({0,0,0},&front);if(front)return 1;
    pcv::render::RenderSnapshot valid;
    valid.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    valid.states = {pcv::render::PointState::Normal, pcv::render::PointState::Plane};
    valid.revision = 1;
    QString error;
    if (!pcv::render::validateSnapshot(valid, &error)) return 1;
    valid.states.resize(1);
    if (pcv::render::validateSnapshot(valid, &error) || error.isEmpty()) return 1;
    pcv::render::CoordinateFrame frame;
    frame.valid = true;
    frame.originInRobotBase.setX(std::numeric_limits<float>::quiet_NaN());
    if (pcv::render::validateCoordinateFrame(frame, &error) || error.isEmpty()) return 1;
    pcv::render::Contour contour;
    contour.points = {QVector3D(0.0f, 0.0f, 0.0f),
                      QVector3D(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)};
    if (pcv::render::validateContours({contour}, &error) || error.isEmpty()) return 1;
    pcv::render::PointColorOverlay colors;
    colors.colors = {QVector4D(0.0f, 0.5f, 1.0f, 1.0f),
                     QVector4D(1.0f, 1.0f, 1.0f, 0.0f)};
    colors.valid = {1, 0};
    if (!pcv::render::validatePointColorOverlay(colors, 2, &error)) return 1;
    if (pcv::render::validatePointColorOverlay(colors, 1, &error) || error.isEmpty()) return 1;
    colors.colors[0].setX(std::numeric_limits<float>::quiet_NaN());
    if (pcv::render::validatePointColorOverlay(colors, 2, &error) || error.isEmpty()) return 1;
    pcv::render::PlanarMaskOverlay mask;
    mask.vertices = {QVector3D(0,0,0), QVector3D(1,0,0), QVector3D(1,1,0), QVector3D(0,1,0)};
    mask.triangleIndices = {0,1,2,0,2,3};
    mask.boundaryVertices = {QVector3D(0,0,0), QVector3D(1,0,0), QVector3D(1,1,0),
                             QVector3D(0,1,0), QVector3D(0,0,0)};
    mask.boundaryRanges = {{0,5}};
    if (!pcv::render::validatePlanarMaskOverlay(mask, &error)) return 1;
    mask.boundaryRanges.clear();
    if (pcv::render::validatePlanarMaskOverlay(mask, &error) || error.isEmpty()) return 1;
    mask.boundaryRanges = {{0,5}};
    mask.boundaryVertices.last().setX(0.1f);
    if (pcv::render::validatePlanarMaskOverlay(mask, &error) || error.isEmpty()) return 1;
    const pcv::render::RenderOverlayOptions defaults;
    if (!defaults.showCoordinateAxes || defaults.showFloorGrid || !defaults.floorZUp
        || !defaults.showAxisLabels || defaults.showNormals || defaults.showBoundingBox
        || defaults.showStatistics || !defaults.showColorBar || !defaults.showOrientation) return 1;
    pcv::render::RenderOverlayOptions hidden;
    hidden.showCoordinateAxes = false;
    hidden.showFloorGrid = false;
    hidden.floorZUp = false;
    hidden.showAxisLabels = false;
    hidden.showNormals = true;
    hidden.showBoundingBox = false;
    hidden.showStatistics = false;
    if (hidden.showCoordinateAxes || hidden.showFloorGrid || hidden.floorZUp
        || hidden.showAxisLabels || !hidden.showNormals || hidden.showBoundingBox
        || hidden.showStatistics) return 1;

    const pcv::render::internal::OverlayBounds bounds = {
        -74.4f, 166.6f, -30.0f, 90.0f, 5.2f, 805.1f};
    const pcv::render::internal::OverlayGeometry geometry =
        pcv::render::internal::makeOverlayGeometry(bounds);
    if (geometry.corners.size() != 8 || geometry.boxEdges.size() != 12) return 1;
    if (!geometry.axes[0].valid || !geometry.axes[1].valid || !geometry.axes[2].valid) return 1;
    if (geometry.axes[0].start.y() != bounds.minY || geometry.axes[0].start.z() != bounds.minZ
        || geometry.axes[0].end.x() != bounds.maxX) return 1;
    if (geometry.axes[1].start.x() != bounds.minX || geometry.axes[1].start.y() != bounds.minY
        || geometry.axes[1].start.z() != bounds.minZ || geometry.axes[1].end.y() != bounds.maxY) return 1;
    if (geometry.axes[2].start.x() != bounds.minX || geometry.axes[2].start.y() != bounds.minY
        || geometry.axes[2].start.z() != bounds.minZ || geometry.axes[2].end.z() != bounds.maxZ) return 1;
    for (const pcv::render::internal::OverlaySegment &axis : geometry.axes) {
        if (axis.startCorner != 0) return 1;
    }
    if (!pcv::render::internal::isAxisSegment(geometry.boxEdges[0], geometry)
        || !pcv::render::internal::isAxisSegment(geometry.boxEdges[3], geometry)
        || !pcv::render::internal::isAxisSegment(geometry.boxEdges[8], geometry)) return 1;

    const QVector<pcv::render::internal::OverlayTick> ticks =
        pcv::render::internal::makeIntegerTicks(bounds.minX, bounds.maxX);
    if (pcv::render::internal::kOverlayGridDivisions != 10) return 1;
    if (ticks.size() < 2 || ticks.first().normalized != 0.0f || ticks.last().normalized != 1.0f
        || ticks.first().value != bounds.minX || ticks.last().value != bounds.maxX) return 1;
    const QVector<pcv::render::internal::OverlayTick> labelTicks =
        pcv::render::internal::makeAxisLabelTicks(bounds.minX, bounds.maxX);
    if (labelTicks.isEmpty() || qFuzzyIsNull(labelTicks.first().normalized)
        || labelTicks.last().normalized != 1.0f) return 1;
    qint64 previous = std::numeric_limits<qint64>::min();
    for (int index = 0; index < ticks.size(); ++index) {
        const pcv::render::internal::OverlayTick &tick = ticks[index];
        const bool isFinalEndpoint = index + 1 == ticks.size();
        if (tick.normalized < 0.0f || tick.normalized > 1.0f
            || (tick.roundedValue <= previous && !isFinalEndpoint)
            || QString::number(tick.roundedValue).contains(QLatin1Char('.'))) return 1;
        previous = tick.roundedValue;
    }
    if (pcv::render::internal::makeIntegerTicks(0.1f, 0.2f).size() != 2
        || !pcv::render::internal::makeIntegerTicks(
                std::numeric_limits<float>::quiet_NaN(), 1.0f).isEmpty()) return 1;
    const pcv::render::internal::OverlayGeometry degenerate =
        pcv::render::internal::makeOverlayGeometry({1.0f, 1.0f, 2.0f, 4.0f, 3.0f, 5.0f});
    if (degenerate.axes[0].valid || !degenerate.axes[1].valid || !degenerate.axes[2].valid) return 1;
    const pcv::render::internal::OverlayGeometry invalid =
        pcv::render::internal::makeOverlayGeometry(
            {0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f, 0.0f, 1.0f});
    if (!invalid.corners.isEmpty() || !invalid.boxEdges.isEmpty()) return 1;
    return 0;
}
