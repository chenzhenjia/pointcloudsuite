#include "pointcloudprocessor.h"

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

QVector<pointcloud::Point3D> makeReferencePlane(QVector<int> *planeIndices) {
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y <= 10; ++y) {
        for (int x = 0; x <= 10; ++x) {
            planeIndices->push_back(points.size());
            points.push_back({float(x), float(y), 10.0f, 0.0f, 0.0f, 1.0f});
        }
    }
    return points;
}

bool detectsConnectedObstacle() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            points.push_back({3.0f + 0.4f * x, 3.0f + 0.4f * y, 12.0f});
    points.push_back({8.0f, 8.0f, 15.0f});

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.6f;
    options.minimumPointCount = 10;
    options.minimumArea = 1.0f;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    return expect(result.ok, "connected obstacle detection should succeed")
        && expect(result.regions.size() == 1, "one connected obstacle should be retained")
        && expect(result.obstacleIndices.size() == 25, "isolated high point should be filtered")
        && expect(std::abs(result.regions[0].meanHeight - 2.0f) < 1.0e-5f,
                  "mean obstacle height should use signed plane distance");
}

bool ignoresLowAndOutsidePoints() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int i = 0; i < 20; ++i) {
        points.push_back({2.0f + 0.05f * i, 2.0f, 10.5f});
        points.push_back({20.0f + 0.05f * i, 20.0f, 14.0f});
    }

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.5f;
    options.minimumPointCount = 5;
    options.minimumArea = 0.0f;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    return expect(result.ok, "empty obstacle result should still succeed")
        && expect(result.regions.isEmpty(), "low and out-of-footprint points should be ignored")
        && expect(result.obstacleIndices.isEmpty(), "no obstacle points should be returned");
}

bool detectsBothPlaneSidesSeparately() {
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> points = makeReferencePlane(&planeIndices);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float px = 4.0f + 0.3f * x;
            const float py = 4.0f + 0.3f * y;
            points.push_back({px, py, 12.0f});
            points.push_back({px, py, 8.0f});
        }
    }

    pointcloud::ObstacleDetectionOptions options;
    options.minimumHeight = 1.0f;
    options.gridSize = 0.5f;
    options.minimumPointCount = 8;
    options.minimumArea = 0.0f;
    options.connectivityRadiusCells = 2;
    const pointcloud::PlaneModel plane{0.0f, 0.0f, 1.0f, -10.0f};
    const auto result = pointcloud::detectObstacles(points, planeIndices, plane, options);

    int positiveRegions = 0;
    int negativeRegions = 0;
    for (const auto &region : result.regions) {
        if (region.sideSign > 0) ++positiveRegions;
        if (region.sideSign < 0) ++negativeRegions;
    }
    return expect(result.ok, "two-sided obstacle detection should succeed")
        && expect(result.regions.size() == 2, "overlapping plane sides must remain separate")
        && expect(positiveRegions == 1, "positive-side obstacle should be retained")
        && expect(negativeRegions == 1, "negative-side obstacle should be retained")
        && expect(result.positiveCandidatePointCount == 16,
                  "positive candidate count should be reported")
        && expect(result.negativeCandidatePointCount == 16,
                  "negative candidate count should be reported")
        && expect(result.obstacleIndices.size() == 32,
                  "all significant points on both sides should be marked");
}

} // namespace

int main() {
    const bool ok = detectsConnectedObstacle()
        && ignoresLowAndOutsidePoints()
        && detectsBothPlaneSidesSeparately();
    if (ok) std::cout << "Obstacle detection tests passed\n";
    return ok ? 0 : 1;
}
