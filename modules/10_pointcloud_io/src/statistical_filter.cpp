#include <pcv/filtering/statistical_filter.h>

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {
struct GridKey {
    qint64 x = 0, y = 0, z = 0;
    bool operator==(const GridKey &other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};
struct GridKeyHash {
    size_t operator()(const GridKey &key) const {
        return size_t(key.x * 73856093LL) ^ size_t(key.y * 19349663LL)
             ^ size_t(key.z * 83492791LL);
    }
};
GridKey keyFor(const pointcloud::Point3D &point, float cellSize) {
    return {qFloor(double(point.x) / cellSize), qFloor(double(point.y) / cellSize),
            qFloor(double(point.z) / cellSize)};
}
bool valid(const pointcloud::Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
const std::array<std::vector<GridKey>, 9> &shellOffsets() {
    static const std::array<std::vector<GridKey>, 9> shells = [] {
        std::array<std::vector<GridKey>, 9> result;
        for (int radius = 0; radius <= 8; ++radius) {
            auto &shell = result[size_t(radius)];
            const int side = 2 * radius + 1;
            shell.reserve(size_t(side * side * side));
            for (qint64 z = -radius; z <= radius; ++z)
                for (qint64 y = -radius; y <= radius; ++y)
                    for (qint64 x = -radius; x <= radius; ++x)
                        if (std::max({qAbs(x), qAbs(y), qAbs(z)}) == radius)
                            shell.push_back({x, y, z});
        }
        return result;
    }();
    return shells;
}
}

namespace pcv::detail::filtering {

float estimateCellSize(const QVector<pointcloud::Point3D> &points) {
    if (points.size() < 2) return 1.0f;
    constexpr int maximumSamples = 4096;
    const int step = qMax(1, int((points.size() + maximumSamples - 1) / maximumSamples));
    std::array<QVector<float>, 3> coordinates;
    for (auto &axis : coordinates) axis.reserve(qMin(maximumSamples, int(points.size())));
    for (int index = 0; index < points.size(); index += step) {
        if (!valid(points[index])) continue;
        coordinates[0].push_back(points[index].x);
        coordinates[1].push_back(points[index].y);
        coordinates[2].push_back(points[index].z);
    }
    if (coordinates[0].size() < 2) return 1.0f;
    std::array<float, 3> spans{};
    for (int axis = 0; axis < 3; ++axis) {
        auto &values = coordinates[axis];
        std::sort(values.begin(), values.end());
        const int trim = values.size() >= 32 ? qMax(1, int(values.size() / 100)) : 0;
        spans[axis] = qMax(0.0f, values[values.size() - 1 - trim] - values[trim]);
    }
    std::sort(spans.begin(), spans.end(), std::greater<float>());
    const float largest = qMax(spans[0], 1.0e-5f);
    int dimensions = 1;
    if (spans[1] > largest * 1.0e-4f) dimensions = 2;
    if (spans[2] > largest * 1.0e-4f) dimensions = 3;
    double extentProduct = 1.0;
    for (int axis = 0; axis < dimensions; ++axis)
        extentProduct *= qMax(double(spans[axis]), double(largest) * 1.0e-6);
    const double spacing = std::pow(extentProduct / qMax<qsizetype>(1, points.size()),
                                    1.0 / double(dimensions));
    return qMax(float(spacing * 1.5), largest * 1.0e-7f);
}

StatisticalFilterResult removeStatisticalOutliers(
    const QVector<pointcloud::Point3D> &points, const StatisticalFilterOptions &options) {
    StatisticalFilterResult result;
    if (points.size() < 3) {
        result.points = points;
        result.warning = QStringLiteral("点数不足，已跳过统计离群值去除");
        return result;
    }
    result.usedCellSize = options.cellSize > 0.0f ? options.cellSize : estimateCellSize(points);
    result.usedCellSize = qMax(result.usedCellSize, 1.0e-7f);
    std::unordered_map<GridKey, std::vector<int>, GridKeyHash> grid;
    grid.reserve(size_t(points.size() / 2 + 1));
    for (int index = 0; index < points.size(); ++index)
        if (valid(points[index])) grid[keyFor(points[index], result.usedCellSize)].push_back(index);

    const int k = qBound(1, options.meanK, int(points.size() - 1));
    const int minimumNeighbors = qMin(k, qMax(2, k / 4));
    QVector<float> meanDistances(points.size(), std::numeric_limits<float>::quiet_NaN());
    for (int index = 0; index < points.size(); ++index) {
        if (!valid(points[index])) continue;
        const GridKey key = keyFor(points[index], result.usedCellSize);
        std::vector<float> nearestSquared;
        nearestSquared.reserve(size_t(k) * 2);
        for (int radius = 0; radius <= 8; ++radius) {
            for (const GridKey &offset : shellOffsets()[size_t(radius)]) {
                const auto cell = grid.find({key.x + offset.x, key.y + offset.y, key.z + offset.z});
                if (cell == grid.end()) continue;
                for (int neighbor : cell->second) {
                    if (neighbor == index) continue;
                    const auto &a = points[index]; const auto &b = points[neighbor];
                    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
                    nearestSquared.push_back(x * x + y * y + z * z);
                }
            }
            if (nearestSquared.size() >= size_t(k)) break;
        }
        if (nearestSquared.size() < size_t(minimumNeighbors)) continue;
        const int count = qMin(k, int(nearestSquared.size()));
        if (nearestSquared.size() > size_t(count))
            std::nth_element(nearestSquared.begin(), nearestSquared.begin() + count,
                             nearestSquared.end());
        double sum = 0.0;
        for (int neighbor = 0; neighbor < count; ++neighbor)
            sum += std::sqrt(double(nearestSquared[size_t(neighbor)]));
        meanDistances[index] = float(sum / double(count));
        ++result.measuredPointCount;
    }
    const int minimumSamples = qMin(int(points.size()), qMax(3, k + 1));
    if (result.measuredPointCount < minimumSamples) {
        result.points = points;
        result.warning = QStringLiteral("有效邻域不足（%1/%2），已保留当前点云；请降低 K 或增大体素尺寸")
            .arg(result.measuredPointCount).arg(minimumSamples);
        return result;
    }
    double mean = 0.0;
    for (float value : meanDistances) if (std::isfinite(value)) mean += value;
    mean /= result.measuredPointCount;
    double variance = 0.0;
    for (float value : meanDistances) if (std::isfinite(value)) {
        const double delta = value - mean; variance += delta * delta;
    }
    const double deviation = result.measuredPointCount > 1
        ? std::sqrt(variance / double(result.measuredPointCount - 1)) : 0.0;
    result.distanceThreshold = float(mean + qMax(0.0f, options.stddevMultiplier) * deviation);
    result.points.reserve(points.size());
    for (int index = 0; index < points.size(); ++index)
        if (std::isfinite(meanDistances[index])
            && meanDistances[index] <= result.distanceThreshold)
            result.points.push_back(points[index]);
    if (result.points.isEmpty()) {
        result.points = points;
        result.warning = QStringLiteral("统计结果无有效保留点，已保留当前点云；请增大阈值倍数");
    }
    return result;
}

} // namespace pcv::detail::filtering
