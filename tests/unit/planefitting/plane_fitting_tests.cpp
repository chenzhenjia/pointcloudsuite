#include <pcv/planefitting/plane_fitting.h>

#include <QCoreApplication>

#include <iostream>

namespace {
bool require(bool value, const char *message) { if (!value) std::cerr << message << '\n'; return value; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const pcv::planefitting::Options defaults;
    if (defaults.ransacIterations != 300 || defaults.pcaRefinementIterations != 2
        || defaults.randomSeed != 20260813u
        || defaults.minimumDisconnectedComponentPoints != 30) return 1;
    QVector<pointcloud::Point3D> points;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        points.push_back({float(x), float(y), 0.01f * float((x + y) % 2)});
    pcv::planefitting::Options options;
    options.initialTolerance = 1.0f;
    options.surfaceTolerance = 0.1f;
    options.minInliers = 3;
    const auto fitted = pcv::planefitting::fit(points, {0, 3, 12}, options);
    if (!require(fitted.ok && fitted.model.c > 0.99f && fitted.planeIndices.size() == points.size(),
                 "independent plane fitting should classify the horizontal cloud")) return 1;
    if (!require(!pcv::planefitting::fit(points, {0, 1}, options).ok,
                 "plane fitting must reject fewer than three controls")) return 1;
    if (!require(!pcv::planefitting::fit(points, {0, 1, 1}, options).ok,
                 "plane fitting must reject duplicate controls")) return 1;
    const auto center = pcv::planefitting::calculateBoundsCenter(points, {0, 3, 12});
    if (!require(center.ok && center.center == QVector3D(1.5f, 1.5f, 0.005f),
                 "bounds center should be calculated from finite indexed points")) return 1;
    if (!require(!pcv::planefitting::calculateBoundsCenter(points, {99}).ok,
                 "bounds center must reject out-of-range indices")) return 1;
    std::cout << "plane_fitting_tests: PASS\n";
    return 0;
}
