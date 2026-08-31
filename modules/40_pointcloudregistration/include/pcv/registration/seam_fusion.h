#pragma once

#include <pcv/registration/multiframe_registration.h>
#include <pcv/registration/seam_fusion_types.h>

#include <QVector>

#include <functional>

namespace pointcloud {

SeamFusionResult applyTrajectorySeamFusion(
    MultiFrameRegistrationResult *merge,
    const SeamFusionOptions &options);

} // namespace pointcloud
