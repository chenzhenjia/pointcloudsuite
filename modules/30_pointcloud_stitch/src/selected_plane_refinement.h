#pragma once
#include <pcv/stitching/selected_plane_stitch.h>

namespace pointcloud {
pcv::stitching::SelectedPlanePairDiagnostic refineSelectedPlanePair(
    const pcv::stitching::SelectedPlaneFrame &source,
    const pcv::stitching::SelectedPlaneFrame &target,
    const pcv::stitching::SelectedPlaneOptions &options,
    const std::function<bool()> &cancelled,
    const ProgressCallback &progress);
}
