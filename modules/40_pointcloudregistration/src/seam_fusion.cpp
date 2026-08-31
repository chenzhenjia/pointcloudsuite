#include <pcv/registration/seam_fusion.h>

namespace pointcloud {

SeamFusionResult applyTrajectorySeamFusion(MultiFrameRegistrationResult *merge,
                                           const SeamFusionOptions &options)
{
    SeamFusionResult result;
    if (!merge) {
        result.error = QStringLiteral("接缝输入为空");
        return result;
    }

    result.inputPoints = merge->points.size();
    result.outputPoints = result.inputPoints;
    if (!options.enabled) {
        result.ok = true;
        return result;
    }
    if (options.isCancelled && options.isCancelled()) {
        result.cancelled = true;
        return result;
    }
    if (merge->points.size() != merge->cloudIds.size()
        || merge->points.size() != merge->sourceIndices.size()
        || merge->points.size() != merge->scanRatios.size()) {
        result.error = QStringLiteral("接缝输入点与来源映射长度不一致");
        return result;
    }
    if (merge->frameMetadata.size() < 2
        || merge->registrationCorrections.size() != merge->frameMetadata.size()) {
        result.error = QStringLiteral("接缝帧元数据不完整");
        return result;
    }
    for (int cloudId : merge->cloudIds) {
        if (cloudId < 0 || cloudId >= merge->frameMetadata.size()) {
            result.error = QStringLiteral("接缝点云来源索引无效");
            return result;
        }
    }

    // The previous implementation can exceed the available memory while it
    // keeps full input, output and spatial-hash copies. Keep the public API
    // fail-closed until the bounded-memory implementation is validated on
    // representative multi-frame data.
    result.error = QStringLiteral("当前版本暂时禁用渐变接缝融合");
    return result;
}

} // namespace pointcloud
