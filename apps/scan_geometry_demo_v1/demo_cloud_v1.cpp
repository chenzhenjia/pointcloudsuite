#include "demo_cloud_v1.h"

namespace pcv::demo {
DemoSegmentsV1 makeDemoSegmentsV1() {
    DemoSegmentsV1 segments;
    int id = 0;
    for (int segment = 0; segment < 6; ++segment) {
        auto append = [&](DemoPlaneV1 plane, float x, float y, float z) {
            segments.at(segment).append({id++, segment, plane, {x, y, z, 0, 0, 1}});
        };
        for (int row = 0; row < 15; ++row) {
            for (int col = 0; col < 12; ++col) {
                const float x = float(segment * 10 + col) * 7;
                append(DemoPlaneV1::A, x, float(row) * 12, 0);
                if (row < 4) append(DemoPlaneV1::B, x, -24 - float(row) * 10, -14);
            }
        }
        if (segment == 3) {
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 5; ++col)
                    append(DemoPlaneV1::C, 223 + float(col) * 7, 60 + float(row) * 14, 24);
        }
        for (int n = 0; n < 12; ++n)
            append(DemoPlaneV1::Noise, float(segment * 10 + n) * 7, 200 + float(n % 3) * 14, float(n % 4) * 3);
    }
    return segments;
}

QString planeNameV1(DemoPlaneV1 plane) {
    switch (plane) {
    case DemoPlaneV1::A: return QStringLiteral("面 A");
    case DemoPlaneV1::B: return QStringLiteral("面 B");
    case DemoPlaneV1::C: return QStringLiteral("面 C");
    case DemoPlaneV1::Noise: return QStringLiteral("杂点");
    default: return QStringLiteral("未选择");
    }
}
}
