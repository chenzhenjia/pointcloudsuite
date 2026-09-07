#pragma once

#include <pcv/core/point_types.h>

#include <QString>
#include <QVector>
#include <QVector3D>

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
    float connectivityRadius = 0.0f;
    int ransacIterations = 300;
    int minInliers = 100;
    unsigned int randomSeed = 20260813u;
    bool keepSeedComponentOnly = true;
    bool useZAxisResidual = false;
    float maxNormalTiltDegrees = 45.0f;
    int pcaRefinementIterations = 2;
    int minimumDisconnectedComponentPoints = 30;
    float minimumDisconnectedComponentRatio = 0.005f;
    bool deferFinalClassification = false;
};

struct Result {
    PlaneModel model;
    QVector<int> candidateIndices;
    QVector<int> planeIndices;
    QVector<int> disconnectedPlaneIndices;
    QVector<pointcloud::Point3D> controlPoints;
    QVector<pointcloud::Point3D> planePoints;
    float rmsError = 0.0f;
    float usedThreshold = 0.0f;
    float planarity = 0.0f;
    int pcaRefinementCount = 0;
    int connectedComponentCount = 0;
    int significantComponentCount = 0;
    bool deferred = false;
    QString error;
    bool ok = false;
};

enum class ConsistencyStatus : quint8 {
    Passed, InvalidInput, ReusedPoint, Collinear, AngleExceeded, DistanceExceeded
};

struct ConsistencyResult {
    ConsistencyStatus status = ConsistencyStatus::InvalidInput;
    float normalAngleDegrees = 0.0f;
    float maximumDistanceMm = 0.0f;
    QString error;
    bool passed = false;
};

struct BoundsCenterResult {
    QVector3D center;
    QString error;
    bool ok = false;
};

Result fit(const QVector<pointcloud::Point3D> &points,
           const QVector<int> &seedIndices,
           const Options &options = {});

ConsistencyResult validateConsistency(
    const QVector<pointcloud::Point3D> &points,
    const PlaneModel &referencePlane,
    const QVector<int> &referenceIndices,
    const QVector<int> &verificationIndices,
    float angleToleranceDegrees = 1.0f,
    float distanceToleranceMm = 0.4f);

BoundsCenterResult calculateBoundsCenter(
    const QVector<pointcloud::Point3D> &points,
    const QVector<int> &indices);

} // namespace pcv::planefitting
