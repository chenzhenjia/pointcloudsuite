#pragma once

#include "pointcloudprocessor.h"
#include <pcv/registration/seam_fusion_types.h>

#include <QVector>
#include <QString>

using SeamFusionOptions = pointcloud::SeamFusionOptions;
using SeamFusionDiagnostic = pointcloud::SeamFusionDiagnostic;
using SeamFusionResult = pointcloud::SeamFusionResult;

SeamFusionResult applyTrajectorySeamFusion(
    pointcloud::WorldCloudMergeResult *merge,
    const QVector<pointcloud::WorldCloudInput> &inputs,
    const SeamFusionOptions &options);
