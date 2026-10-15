#pragma once

#include <pcv/planefitting/plane_fitting.h>
#include <memory>

namespace pcv::planefitting {

class PlaneSelectionIndex;
using PlaneSelectionIndexPtr = std::shared_ptr<const PlaneSelectionIndex>;

enum class SingleClickStatus { Success, InvalidInput, InsufficientSupport, Ambiguous, Cancelled };

struct PlaneSelectionIndexResult {
    PlaneSelectionIndexPtr index;
    QString error;
    bool cancelled = false;
    double preparationMs = 0.0;
};

struct SingleClickOptions {
    float surfaceToleranceMm = 0.4f;
    bool parallelEnabled = false;
    int maxParallelTasks = 4;
    std::function<bool()> isCancelled;
    float maximumGapMm = 3.0f;
};

struct SingleClickResult {
    SingleClickStatus status = SingleClickStatus::InvalidInput;
    PlaneModel model;
    QVector<int> planeIndices;
    float rmsMm = 0.0f;
    float localSupportRatio = 0.0f;
    float localSpacingMm = 0.0f;
    float connectivityRadiusMm = 0.0f;
    float gridSizeMm = 0.0f;
    StageTimings timings;
    QString error;
    float samplingSpacingUMm = 0.0f;
    float samplingSpacingVMm = 0.0f;
    float connectivityRadiusUMm = 0.0f;
    float connectivityRadiusVMm = 0.0f;
    float maximumGapMm = 0.0f;
    int mergedRegionCount = 0;
    int refinementIterations = 0;
    bool refinementConverged = false;
    double gapMergeMs = 0.0;
    bool ok() const { return status == SingleClickStatus::Success; }
};

// The immutable index retains a const shared view of this exact point buffer.
// Replaced/edited buffers must be prepared again. No original point is modified.
PlaneSelectionIndexResult preparePlaneSelectionIndex(
    const QVector<pointcloud::Point3D> &points,
    const std::function<bool()> &isCancelled = {});

SingleClickResult selectPlaneAtPoint(
    const QVector<pointcloud::Point3D> &points,
    const PlaneSelectionIndexPtr &index,
    int clickedIndex,
    const SingleClickOptions &options = {});

} // namespace pcv::planefitting
