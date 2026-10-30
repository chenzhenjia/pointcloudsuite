#pragma once

#include <pcv/planefitting/single_click_plane_selection_v1.h>
#include <QSet>
#include <QVector3D>

namespace pcv::planefitting {

struct HighSidePlaneCandidateV1 {
    int seedIndex = -1;
    int pointCount = 0;
    // Candidate bounds-center Z minus selected workpiece center Z, in mm.
    float heightMm = 0.0f;
};

struct HighSidePlaneSearchOptionsV1 {
    float minimumHeightMm = 2.0f;
    int minimumPointCount = 100;
    int maximumSeeds = 16;
    SingleClickOptions selection;
};

enum class HighSidePlaneSearchStatusV1 { Success, InvalidInput, Cancelled, Error };

struct HighSidePlaneSearchResultV1 {
    HighSidePlaneSearchStatusV1 status = HighSidePlaneSearchStatusV1::InvalidInput;
    QVector<HighSidePlaneCandidateV1> candidates;
    int eligibleBuckets = 0;
    int attemptedSeeds = 0;
    QString error;
};

// Searches full, already transformed robot_base points. Height is candidate
// center Z minus selected workpiece center Z in world coordinates.
HighSidePlaneSearchResultV1 findHighSidePlaneCandidatesV1(
    const QVector<pointcloud::Point3D> &points,
    const PlaneSelectionIndexPtr &index,
    const QVector3D &selectedCenter,
    const QVector<int> &excludedIndices,
    const HighSidePlaneSearchOptionsV1 &options = {});

} // namespace pcv::planefitting
