#pragma once

#include <pcv/stitching/world_cloud_types.h>
#include <pcv/registration/multiframe_registration.h>

namespace pointcloud {

// Compatibility aliases retained while application call sites migrate to the
// module-owned stitching and registration contracts.
using WorldCloudInput = pcv::stitching::WorldCloudInput;
using WorldCloudMergeResult = MultiFrameRegistrationResult;

WorldCloudMergeResult mergePlyCloudsInWorld(
    const QVector<WorldCloudInput> &inputs,
    const IcpOptions &icp = {},
    const ProgressCallback &progress = {});

} // namespace pointcloud
