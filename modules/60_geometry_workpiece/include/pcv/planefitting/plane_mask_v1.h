#pragma once

#include <pcv/planefitting/plane_fitting.h>
#include <pcv/core/processing_budget.h>
#include <QPair>
#include <QJsonObject>
#include <QVector>
#include <QVector3D>
#include <functional>
#include <memory>

namespace pcv::planefitting {

struct PlaneMaskMeshV1 {
    QVector<QVector3D> vertices;
    QVector<quint32> triangleIndices;
    QVector<QVector3D> boundaryVertices;
    QVector<QPair<int, int>> boundaryRanges;
};

struct PlaneMaskOptionsV1 {
    QVector3D samplingAxisU;
    double spacingUMm = 0.0;
    double spacingVMm = 0.0;
    std::function<bool()> isCancelled;
    std::shared_ptr<pcv::ProcessingBudget> processingBudget;
    bool preferDenseGrid = true;
    bool fixedGridOrigin = false;
    double gridOriginU = 0, gridOriginV = 0;
};

struct PlaneMaskResultV1 {
    PlaneMaskMeshV1 mesh;
    QString error;
    bool ok = false;
    bool cancelled = false;
    bool outOfMemory = false;
    QJsonObject performance; // Session diagnostics, not an output contract.
    double spacingUMm = 0, spacingVMm = 0; // Effective spacing, for stable display rebuilds.
    double gridOriginU = 0, gridOriginV = 0;
};

// Builds a planar occupancy mask from complete, original inlier indices.
PlaneMaskResultV1 buildPlaneMaskV1(
    const QVector<pointcloud::Point3D> &points,
    const QVector<int> &planeIndices,
    const PlaneModel &model,
    const PlaneMaskOptionsV1 &options = {});

} // namespace pcv::planefitting
