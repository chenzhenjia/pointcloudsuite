#pragma once

#include <QString>
#include <QVector>

namespace pointcloud {

struct Point3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    // Optional per-point normal from the PLY six-dimensional point record.
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
};

struct LoadResult {
    QVector<Point3D> points;
    QString error;
    bool ok = false;
    bool usedCache = false;
};

// Loads ASCII PLY and binary little/big-endian PLY vertex xyz data.
// RGB and other vertex properties are skipped safely.
bool loadPly(const QString &fileName, QVector<Point3D> &points,
             QString *error = nullptr);

// Loads a PLY through a validated binary cache next to the source file.
// The cache is created after the first successful PLY parse.
bool loadPlyCached(const QString &fileName, QVector<Point3D> &points,
                   QString *error = nullptr, bool *usedCache = nullptr);

LoadResult loadPlyCachedResult(const QString &fileName);

// HiViewer-style proportional thinning. denominator=1 keeps all points,
// denominator=2 keeps approximately 1/2, denominator=16 keeps approximately 1/16.
QVector<Point3D> proportionalDownsample(const QVector<Point3D> &points,
                                         int denominator);

// Spatial octree LOD: returns one representative point per occupied octree
// leaf, bounded by targetCount. The input order is preserved only loosely.
QVector<Point3D> octreeLod(const QVector<Point3D> &points, qsizetype targetCount);

} // namespace pointcloud
