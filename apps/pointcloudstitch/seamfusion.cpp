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
    pointcloud::SeamFusionOptions sharedOptions;
    sharedOptions.enabled = options.enabled;
    sharedOptions.halfWidth = options.halfWidth;
    sharedOptions.mutualDistance = options.mutualDistance;
    sharedOptions.decisionCellSize = options.decisionCellSize;
    const auto shared = pointcloud::applyTrajectorySeamFusionKernel(
        &merge->points, &merge->cloudIds, &merge->sourceIndices, &merge->scanRatios,
        starts, ends, merge->registrationCorrections, sharedOptions);
    result.inputPoints = shared.inputPoints;
    result.outputPoints = shared.outputPoints;
    result.error = shared.error;
    result.ok = shared.ok;
    result.cancelled = shared.cancelled;
    for (const auto &d : shared.diagnostics) {
        SeamFusionDiagnostic converted;
        converted.cloudA = d.cloudA; converted.cloudB = d.cloudB;
        converted.bandPoints = d.bandPoints; converted.bandPointsA = d.bandPointsA; converted.bandPointsB = d.bandPointsB;
        converted.mutualPairs = d.mutualPairs; converted.interpolatedPoints = d.interpolatedPoints;
        converted.unmatchedDiscarded = d.unmatchedDiscarded; converted.unmatchedPreserved = d.unmatchedPreserved;
        converted.decisionCells = d.decisionCells; converted.corePoints = d.corePoints;
        converted.applied = d.applied; converted.actualOverlapValid = d.actualOverlapValid;
        converted.projectedAMin = d.projectedAMin; converted.projectedAMax = d.projectedAMax;
        converted.projectedBMin = d.projectedBMin; converted.projectedBMax = d.projectedBMax;
        converted.actualOverlapMin = d.actualOverlapMin; converted.actualOverlapMax = d.actualOverlapMax;
        converted.seamProjection = d.seamProjection; converted.reason = d.reason;
        result.diagnostics.push_back(converted);
    }
    return result;
}
