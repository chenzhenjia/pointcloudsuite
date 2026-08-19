#include <pcv/filtering/statistical_filter.h>

#include <QCoreApplication>

#include <limits>
#include <cmath>
#include <iostream>

namespace {
int fail(const char *message) { std::cerr << message << '\n'; return 1; }
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);

    QVector<pointcloud::Point3D> small = {{0, 0, 0}, {1, 0, 0}};
    const auto skipped = pcv::detail::filtering::removeStatisticalOutliers(small);
    if (skipped.points.size() != small.size() || skipped.warning.isEmpty())
        return fail("small cloud fallback failed");

    QVector<pointcloud::Point3D> cloud;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            cloud.push_back({float(x) * 0.1f, float(y) * 0.1f, 0.0f});
    cloud.push_back({100.0f, 100.0f, 100.0f});
    pcv::detail::filtering::StatisticalFilterOptions options;
    options.meanK = 8;
    options.stddevMultiplier = 1.0f;
    options.cellSize = 0.2f;
    const auto filtered = pcv::detail::filtering::removeStatisticalOutliers(cloud, options);
    if (filtered.points.size() >= cloud.size())
        return fail("isolated outlier was not removed");
    for (const auto &point : filtered.points)
        if (point.x == 100.0f) return fail("isolated outlier remained in output");
    if (filtered.measuredPointCount < 90 || filtered.usedCellSize != 0.2f)
        return fail("filter diagnostics are incorrect");

    QVector<pointcloud::Point3D> invalid = cloud;
    invalid.push_back({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    const auto invalidResult = pcv::detail::filtering::removeStatisticalOutliers(invalid, options);
    for (const auto &point : invalidResult.points)
        if (!std::isfinite(point.x)) return fail("invalid point remained in output");

    const float estimated = pcv::detail::filtering::estimateCellSize(cloud);
    if (!(estimated > 0.0f)) return fail("cell-size estimate is not positive");
    return 0;
}
