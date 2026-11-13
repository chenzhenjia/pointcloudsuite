#pragma once
#include <QVector>
#include <QVector3D>
namespace pcv {
struct CadTriangleV1 { int a = 0, b = 0, c = 0, faceId = 0; };
struct CadMeshV1 {
    QVector<QVector3D> vertices;
    QVector<CadTriangleV1> triangles;
};
}
