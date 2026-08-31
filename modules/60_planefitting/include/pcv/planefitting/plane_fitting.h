#pragma once

#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>

namespace pcv::planefitting {

struct PlaneModel {
    float a = 0.0f, b = 0.0f, c = 1.0f, d = 0.0f;
    int inlierCount = 0;
    float meanDistance = 0.0f;
    float maxDistance = 0.0f;
};

struct Options {
    float initialTolerance = 1.0f;
    float surfaceTolerance = 0.4f;
    int minInliers = 100;
    bool useZAxisResidual = false;
    float maxNormalTiltDegrees = 45.0f;
    bool deferFinalClassification = false;
};

struct Result {
    PlaneModel model;
    QVector<int> candidateIndices;
    QVector<int> planeIndices;
    QVector<pointcloud::Point3D> controlPoints;
    QVector<pointcloud::Point3D> planePoints;
    float rmsError = 0.0f;
    float usedThreshold = 0.0f;
    int pcaRefinementCount = 0;
    bool deferred = false;
    QString error;
    bool ok = false;
};

Result fit(const QVector<pointcloud::Point3D> &points,
           const QVector<int> &seedIndices,
           const Options &options = {});

} // namespace pcv::planefitting
