#pragma once

#include <QVector>
#include <QVector3D>
#include <QtGlobal>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

// Private render helpers. They deliberately stay outside the public render
// contract: the generated segments are transient QPainter overlay geometry.
namespace pcv::render::internal {

// Ten normalized intervals keep the axis readable while retaining the full
// [0, 1] coverage (eleven grid lines including both endpoints).
constexpr int kOverlayGridDivisions = 10;
constexpr float kOverlayAxisEpsilon = 1.0e-6f;

struct OverlayBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
};

struct OverlaySegment {
    QVector3D start;
    QVector3D end;
    int startCorner = -1;
    int endCorner = -1;
    bool valid = false;
};

struct OverlayGeometry {
    QVector<QVector3D> corners;
    QVector<OverlaySegment> boxEdges;
    std::array<OverlaySegment, 3> axes;
};

struct OverlayTick {
    float normalized = 0.0f;
    float value = 0.0f;
    qint64 roundedValue = 0;
};

inline bool isFiniteBounds(const OverlayBounds &bounds)
{
    return std::isfinite(bounds.minX) && std::isfinite(bounds.maxX)
        && std::isfinite(bounds.minY) && std::isfinite(bounds.maxY)
        && std::isfinite(bounds.minZ) && std::isfinite(bounds.maxZ)
        && bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY
        && bounds.minZ <= bounds.maxZ;
}

inline OverlaySegment makeSegment(const QVector<QVector3D> &corners, int start, int end)
{
    OverlaySegment segment;
    segment.startCorner = start;
    segment.endCorner = end;
    if (start < 0 || end < 0 || start >= corners.size() || end >= corners.size()) return segment;
    segment.start = corners[start];
    segment.end = corners[end];
    segment.valid = std::isfinite(segment.start.x()) && std::isfinite(segment.start.y())
        && std::isfinite(segment.start.z()) && std::isfinite(segment.end.x())
        && std::isfinite(segment.end.y()) && std::isfinite(segment.end.z())
        && (segment.end - segment.start).lengthSquared() > kOverlayAxisEpsilon * kOverlayAxisEpsilon;
    return segment;
}

inline OverlayGeometry makeOverlayGeometry(const OverlayBounds &bounds)
{
    OverlayGeometry geometry;
    if (!isFiniteBounds(bounds)) return geometry;

    geometry.corners = {
        {bounds.minX, bounds.minY, bounds.minZ}, {bounds.maxX, bounds.minY, bounds.minZ},
        {bounds.maxX, bounds.maxY, bounds.minZ}, {bounds.minX, bounds.maxY, bounds.minZ},
        {bounds.minX, bounds.minY, bounds.maxZ}, {bounds.maxX, bounds.minY, bounds.maxZ},
        {bounds.maxX, bounds.maxY, bounds.maxZ}, {bounds.minX, bounds.maxY, bounds.maxZ}};

    // All axes share the minimum corner as their origin and extend in their
    // positive direction: X=0->1, Y=0->3, Z=0->4.
    constexpr std::array<std::array<int, 2>, 12> boxEdges = {{{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                                                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                                                {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    constexpr std::array<std::array<int, 2>, 3> axisEdges = {{{0, 1}, {0, 3}, {0, 4}}};

    for (int axis = 0; axis < int(axisEdges.size()); ++axis)
        geometry.axes[axis] = makeSegment(geometry.corners, axisEdges[axis][0], axisEdges[axis][1]);
    for (const auto &edge : boxEdges)
        geometry.boxEdges.push_back(makeSegment(geometry.corners, edge[0], edge[1]));
    return geometry;
}

inline bool isAxisSegment(const OverlaySegment &segment, const OverlayGeometry &geometry)
{
    for (const OverlaySegment &axis : geometry.axes) {
        if (axis.valid && ((segment.startCorner == axis.startCorner
                            && segment.endCorner == axis.endCorner)
                           || (segment.startCorner == axis.endCorner
                               && segment.endCorner == axis.startCorner)))
            return true;
    }
    return false;
}

inline QVector<OverlayTick> makeIntegerTicks(float minimum, float maximum,
                                              int divisions = kOverlayGridDivisions)
{
    QVector<OverlayTick> ticks;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum
        || divisions < 1 || maximum - minimum <= kOverlayAxisEpsilon) {
        return ticks;
    }
    constexpr double maxRoundedValue = double(std::numeric_limits<qint64>::max() - 1);
    qint64 previous = std::numeric_limits<qint64>::min();
    for (int i = 0; i <= divisions; ++i) {
        const float normalized = float(i) / float(divisions);
        const float value = minimum + (maximum - minimum) * normalized;
        if (!std::isfinite(value) || std::abs(double(value)) > maxRoundedValue) continue;
        const qint64 rounded = qRound64(value);
        // Consecutive equal interior labels carry no information. Keep both
        // endpoints so the normalized range remains explicitly annotated.
        if (!ticks.isEmpty() && rounded == previous && i != divisions) continue;
        ticks.push_back({normalized, value, rounded});
        previous = rounded;
    }
    return ticks;
}

inline QVector<OverlayTick> makeAxisLabelTicks(float minimum, float maximum,
                                                int divisions = kOverlayGridDivisions)
{
    QVector<OverlayTick> ticks = makeIntegerTicks(minimum, maximum, divisions);
    if (!ticks.isEmpty() && qFuzzyIsNull(ticks.first().normalized)) ticks.removeFirst();
    return ticks;
}

} // namespace pcv::render::internal
