#pragma once

#include <pcv/planefitting/plane_fitting.h>
#include <QVector3D>
#include <memory>
#include <pcv/core/processing_budget.h>

namespace pcv::planefitting {

class PlaneSelectionIndex;
using PlaneSelectionIndexPtr = std::shared_ptr<const PlaneSelectionIndex>;

enum class SingleClickStatus { Success, InvalidInput, InsufficientSupport, Ambiguous, Cancelled };

struct PlaneSelectionIndexResult {
    PlaneSelectionIndexPtr index;
    QString error;
    bool cancelled = false;
    double preparationMs = 0.0;
    double gridMs = 0.0;
    double normalsMs = 0.0;
    double budgetAcquireMs = 0.0; // Included in preparationMs; nested in grid/neighbor/normals phases.
    qsizetype bucketCount = 0;
    int normalBatchSize = 0, normalBatchCount = 0, normalWorkerCount = 1;
    QString normalFallback;
    // Compute is summed worker CPU-wall time; join waits include worker execution.
    // These overlap and must never be added to normalsMs.
    double normalComputeSumMs = 0, normalJoinMs = 0, normalCommitMs = 0;
    double neighborPreparationMs = 0;
    bool neighborCacheUsed = false;
    size_t neighborCacheBytes = 0;
    QString neighborFallback;
    bool pointBucketCacheUsed = false;
    size_t optimizationBytes = 0;
    bool outOfMemory = false;
};

struct SingleClickOptions {
    float surfaceToleranceMm = 0.4f;
    bool parallelEnabled = false;
    int maxParallelTasks = 4;
    std::function<bool()> isCancelled;
    float maximumGapMm = 3.0f;
    std::shared_ptr<pcv::ProcessingBudget> processingBudget;
    bool reuseConnectivityWorkspace = true;
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
    QVector3D samplingAxisU;
    float connectivityRadiusUMm = 0.0f;
    float connectivityRadiusVMm = 0.0f;
    float maximumGapMm = 0.0f;
    int mergedRegionCount = 0;
    int refinementIterations = 0;
    bool refinementConverged = false;
    double gapMergeMs = 0.0;
    bool outOfMemory = false;
    bool ok() const { return status == SingleClickStatus::Success; }
    double projectionMs = 0.0;
    double normalConnectivityMs = 0.0;
    double connectivityBuildMs = 0, connectivityTraversalMs = 0;
    qsizetype connectivityCellCount = 0;
    bool connectivityWorkspaceUsed = false;
};

struct IndexPreparationOptions {
    bool parallelEnabled = false;
    int maxParallelTasks = 4;
    bool cachePointBuckets = false;
    std::shared_ptr<pcv::ProcessingBudget> processingBudget;
    // Scheduling only. Existing callers keep 4096; new GUI explicitly uses 65536.
    int normalBatchSize = 4096;
    bool cacheNormalNeighbors = false;
};

// The immutable index retains a const shared view of this exact point buffer.
// Replaced/edited buffers must be prepared again. No original point is modified.
PlaneSelectionIndexResult preparePlaneSelectionIndex(
    const QVector<pointcloud::Point3D> &points,
    const std::function<bool()> &isCancelled = {},
    const IndexPreparationOptions &options = {});

SingleClickResult selectPlaneAtPoint(
    const QVector<pointcloud::Point3D> &points,
    const PlaneSelectionIndexPtr &index,
    int clickedIndex,
    const SingleClickOptions &options = {});

} // namespace pcv::planefitting
