#pragma once

#include <pcv/core/point_types.h>
#include <pcv/registration/seam_fusion_types.h>

#include <QMatrix4x4>
#include <QVector>

namespace pointcloud {

SeamFusionResult applyTrajectorySeamFusionKernel(
    QVector<Point3D> *points,
    QVector<int> *cloudIds,
    QVector<qsizetype> *sourceIndices,
    QVector<float> *scanRatios,
    const QVector<QMatrix4x4> &frameStarts,
    const QVector<QMatrix4x4> &frameEnds,
    const QVector<QMatrix4x4> &registrationCorrections,
    const SeamFusionOptions &options);

} // namespace pointcloud
