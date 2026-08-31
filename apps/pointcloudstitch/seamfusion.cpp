#include "seamfusion.h"

#include <pcv/registration/seam_fusion_kernel.h>

SeamFusionResult applyTrajectorySeamFusion(
    pointcloud::WorldCloudMergeResult *merge,
    const QVector<pointcloud::WorldCloudInput> &inputs,
    const SeamFusionOptions &options)
{
    SeamFusionResult result;
    if (!merge) { result.error = QStringLiteral("接缝输入为空"); return result; }
    QVector<QMatrix4x4> starts, ends;
    starts.reserve(inputs.size()); ends.reserve(inputs.size());
    for (const auto &input : inputs) { starts.push_back(input.startBaseFromFlange); ends.push_back(input.endBaseFromFlange); }
    const auto shared = pointcloud::applyTrajectorySeamFusionKernel(
        &merge->points, &merge->cloudIds, &merge->sourceIndices, &merge->scanRatios,
        starts, ends, merge->registrationCorrections, options);
    return shared;
}
