#pragma once

#include <pcv/stitching/multiframe_registration.h>
#include <pcv/stitching/seam_fusion_types.h>

#include <QVector>

#include <functional>

namespace pointcloud {

SeamFusionResult applyTrajectorySeamFusion(
    MultiFrameRegistrationResult *merge,
    const SeamFusionOptions &options);

} // namespace pointcloud
