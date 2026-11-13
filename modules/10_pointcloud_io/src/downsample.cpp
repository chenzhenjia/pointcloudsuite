#include <pcv/filtering/downsample.h>

#include <QtMath>

#include <cmath>
#include <unordered_map>

namespace {
struct VoxelKey {
    qint64 x = 0, y = 0, z = 0;
    bool operator==(const VoxelKey &other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};
struct VoxelKeyHash {
    size_t operator()(const VoxelKey &key) const {
        return size_t(key.x * 73856093LL) ^ size_t(key.y * 19349663LL)
             ^ size_t(key.z * 83492791LL);
    }
};
VoxelKey keyFor(const pointcloud::Point3D &point, float voxelSize) {
    return {qFloor(double(point.x) / voxelSize),
            qFloor(double(point.y) / voxelSize),
            qFloor(double(point.z) / voxelSize)};
}
bool valid(const pointcloud::Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
}

namespace pcv::detail::filtering {

QVector<pointcloud::Point3D> proportionalDownsample(
    const QVector<pointcloud::Point3D> &points, int denominator) {
    denominator = qMax(1, denominator);
    if (denominator == 1 || points.isEmpty()) return points;
    QVector<pointcloud::Point3D> result;
    result.reserve((points.size() + denominator - 1) / denominator);
    for (qsizetype index = 0; index < points.size(); index += denominator)
        result.push_back(points[index]);
    return result;
}

VoxelDownsampleResult voxelDownsample(const QVector<pointcloud::Point3D> &points,
                                      float voxelSize,
                                      VoxelRepresentative representative,
                                      bool discardInvalid) {
    VoxelDownsampleResult result;
    if (points.isEmpty() || voxelSize <= 0.0f) {
        result.points = points;
        result.sourceIndices.reserve(points.size());
        for (qsizetype index = 0; index < points.size(); ++index)
            result.sourceIndices.push_back(index);
        return result;
    }
    struct Accumulator {
        pointcloud::Point3D point;
        qsizetype firstIndex = -1;
        qsizetype count = 0;
    };
    std::unordered_map<VoxelKey, qsizetype, VoxelKeyHash> positions;
    QVector<Accumulator> accumulators;
    positions.reserve(size_t(points.size() / 2 + 1));
    for (qsizetype index = 0; index < points.size(); ++index) {
        const auto &point = points[index];
        if (discardInvalid && !valid(point)) continue;
        const VoxelKey key = keyFor(point, voxelSize);
        auto [it, inserted] = positions.emplace(key, accumulators.size());
        if (inserted) {
            Accumulator accumulator;
            accumulator.point = point;
            accumulator.firstIndex = index;
            accumulator.count = 1;
            accumulators.push_back(accumulator);
        } else if (representative == VoxelRepresentative::Centroid) {
            Accumulator &accumulator = accumulators[it->second];
            accumulator.point.x += point.x;
            accumulator.point.y += point.y;
            accumulator.point.z += point.z;
            accumulator.point.nx += point.nx;
            accumulator.point.ny += point.ny;
            accumulator.point.nz += point.nz;
            ++accumulator.count;
        }
    }
    result.points.reserve(accumulators.size());
    result.sourceIndices.reserve(accumulators.size());
    for (const Accumulator &accumulator : accumulators) {
        pointcloud::Point3D point = accumulator.point;
        if (representative == VoxelRepresentative::Centroid) {
            const float inverse = 1.0f / float(accumulator.count);
            point.x *= inverse; point.y *= inverse; point.z *= inverse;
            point.nx *= inverse; point.ny *= inverse; point.nz *= inverse;
        }
        result.points.push_back(point);
        result.sourceIndices.push_back(accumulator.firstIndex);
    }
    return result;
}

} // namespace pcv::detail::filtering
