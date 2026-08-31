#pragma once

#include <pcv/core/point_types.h>

#include <QVector>
#include <QString>

namespace pcv::render {

enum class PointState : quint8 { Normal = 0, Plane = 1, Edge = 2 };

struct RenderSnapshot {
    QVector<pointcloud::Point3D> points;
    QVector<PointState> states;
    quint64 revision = 0;
};

bool validateSnapshot(const RenderSnapshot &snapshot, QString *error = nullptr);

} // namespace pcv::render
