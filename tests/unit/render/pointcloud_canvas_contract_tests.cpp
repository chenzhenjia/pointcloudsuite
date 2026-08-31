#include <pcv/render/pointcloud_canvas_contract.h>

#include <QCoreApplication>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pcv::render::RenderSnapshot valid;
    valid.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    valid.states = {pcv::render::PointState::Normal, pcv::render::PointState::Plane};
    valid.revision = 1;
    QString error;
    if (!pcv::render::validateSnapshot(valid, &error)) return 1;
    valid.states.resize(1);
    if (pcv::render::validateSnapshot(valid, &error) || error.isEmpty()) return 1;
    return 0;
}
