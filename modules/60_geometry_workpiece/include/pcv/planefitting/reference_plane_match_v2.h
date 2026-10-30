#pragma once
#include <pcv/planefitting/single_click_plane_selection_v1.h>

namespace pcv::planefitting {
enum class ReferencePlaneMatchStatusV2 { Unique, Ambiguous, NotFound, Cancelled, InvalidInput, Error };
struct ReferencePlaneMatchOptionsV2 {
    float maximumDistanceMm = 10.0f;
    float maximumNormalAngleDegrees = 5.0f;
    int maximumSeeds = 16;
    SingleClickOptions selection;
};
struct ReferencePlaneCandidateV2 {
    int seedIndex = -1;
    SingleClickResult selection;
    float normalAngleDegrees = 0;
    float referenceDistanceMm = 0;
};
struct ReferencePlaneMatchResultV2 {
    ReferencePlaneMatchStatusV2 status = ReferencePlaneMatchStatusV2::NotFound;
    QVector<ReferencePlaneCandidateV2> candidates;
    int eligibleBuckets = 0, attemptedSeeds = 0;
    QVector<int> attemptedSeedIndices;
    QString error;
    bool outOfMemory = false;
};
// All points and the reference plane are already in robot_base/mm. This API
// proposes selections for human review, never confirms correspondence/quality.
// Ambiguous returns at least two distinct candidates and stops further search.
ReferencePlaneMatchResultV2 matchReferencePlaneV2(
    const QVector<pointcloud::Point3D> &points, const PlaneSelectionIndexPtr &index,
    const PlaneModel &reference, const ReferencePlaneMatchOptionsV2 &options = {});
} // namespace pcv::planefitting
