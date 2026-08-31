#include <pcv/render/pointcloud_canvas_contract.h>

#include <QCoreApplication>

#include <iostream>
#include <limits>

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
    pcv::render::CoordinateFrame frame;
    frame.valid = true;
    frame.originInRobotBase.setX(std::numeric_limits<float>::quiet_NaN());
    if (pcv::render::validateCoordinateFrame(frame, &error) || error.isEmpty()) return 1;
    pcv::render::Contour contour;
    contour.points = {QVector3D(0.0f, 0.0f, 0.0f),
                      QVector3D(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)};
    if (pcv::render::validateContours({contour}, &error) || error.isEmpty()) return 1;
    return 0;
}
