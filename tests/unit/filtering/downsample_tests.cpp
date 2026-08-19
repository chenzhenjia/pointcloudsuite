#include <pcv/filtering/downsample.h>

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {
int fail(const char *message) {
    std::cerr << message << '\n';
    return 1;
}
bool closeTo(float value, float expected) {
    return std::abs(value - expected) < 1.0e-6f;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QVector<pointcloud::Point3D> points = {
        {0.1f, 0.1f, 0.1f}, {0.3f, 0.3f, 0.3f}, {1.2f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}
    };
    const QVector<pointcloud::Point3D> proportional =
        pcv::detail::filtering::proportionalDownsample(points, 2);
    if (proportional.size() != 3 || proportional[1].x != 1.2f)
        return fail("proportional sampling changed ordering");

    const auto first = pcv::detail::filtering::voxelDownsample(
        points, 1.0f, pcv::detail::filtering::VoxelRepresentative::FirstInputPoint);
    if (first.points.size() != 4 || first.sourceIndices[0] != 0
        || first.points[0].x != points[0].x)
        return fail("first-point voxel policy failed");

    const auto centroid = pcv::detail::filtering::voxelDownsample(
        points, 1.0f, pcv::detail::filtering::VoxelRepresentative::Centroid);
    if (centroid.points.size() != 4 || !closeTo(centroid.points[0].x, 0.2f))
        return fail("centroid voxel policy failed");

    const auto unchanged = pcv::detail::filtering::voxelDownsample(points, 0.0f);
    if (unchanged.points.size() != points.size()
        || unchanged.sourceIndices.size() != points.size())
        return fail("disabled voxel sampling did not preserve input");
    return 0;
}
