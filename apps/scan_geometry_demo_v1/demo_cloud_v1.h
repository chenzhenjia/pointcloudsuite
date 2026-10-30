#pragma once

#include <pcv/core/point_types.h>
#include <QVector>
#include <QString>
#include <array>

namespace pcv::demo {
enum class DemoPlaneV1 { None, A, B, C, Noise };
struct DemoPointV1 {
    int id = -1;
    int segment = -1;
    DemoPlaneV1 plane = DemoPlaneV1::None;
    pointcloud::Point3D position;
};
using DemoSegmentsV1 = std::array<QVector<DemoPointV1>, 6>;
DemoSegmentsV1 makeDemoSegmentsV1();
QString planeNameV1(DemoPlaneV1 plane);
}
