#pragma once

#include <pcv/core/cancellation.h>
#include <pcv/core/point_types.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

namespace pointcloud {

using ProgressCallback = std::function<void(float, const QString &)>;

struct RobotBaseFrame {
    QString sourceFile;
    QVector<Point3D> fullPoints;
    QVector<Point3D> samplePoints;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    QMatrix4x4 flangeFromDepth;
    qsizetype declaredCount = 0;
    qsizetype rejectedInvalid = 0;
    qsizetype rejectedRange = 0;
    float inputYMinimum = 0.0f;
    float inputYMaximum = 0.0f;
    float signedTravel = 0.0f;
    int dominantTravelAxis = -1;
};

struct FrameTransformMetadata {
    QString sourceFile;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    QMatrix4x4 flangeFromDepth;
    qsizetype declaredCount = 0;
    qsizetype convertedCount = 0;
    qsizetype rejectedInvalid = 0;
    qsizetype rejectedRange = 0;
    float inputYMinimum = 0.0f;
    float inputYMaximum = 0.0f;
    float signedTravel = 0.0f;
    int dominantTravelAxis = -1;
};

struct IcpOptions {
    bool enabled = true;
    QVector<float> voxelLevels = {1.0f, 0.5f, 0.2f};
    QVector<float> correspondenceDistances = {3.0f, 1.5f, 0.6f};
    int sampleStride = 8;
    float overlapMargin = 5.0f;
    bool planePrealignmentEnabled = true;
    float maximumPlanePrealignmentTranslation = 6.0f;
    float maximumPlanePrealignmentAngleDegrees = 0.5f;
    bool planeIdentityTrackingEnabled = true;
    float planeIdentityHeightToleranceMm = 5.0f;
    float planeFitHeightBandMm = 3.0f;
    float planeFitInlierDistanceMm = 0.8f;
    int planeFitIterations = 3;
    float maximumCorrectionTranslation = 10.0f;
    float maximumCorrectionAngleDegrees = 1.0f;
    bool structuralValidationEnabled = true;
    float structuralPlaneExclusionMm = 3.0f;
    float minimumDegenerateStructuralCoverage3mm = 0.65f;
    float minimumLowCountStructuralCoverage3mm = 0.45f;
    float minimumLowCountStructuralCoverage5mm = 0.80f;
    int minimumStructuralPoints = 100;
    bool relaxedStructuralValidation = false;
    bool structuralIcpEnabled = true;
    int supportPlaneSampleStride = 16;
    // Balanced mode: the expensive in-plane coarse search is off by default.
    // With tangentCoarseAlignmentAuto=true it still runs when the robot initial
    // value, after plane pre-alignment, misses the accepted overlap/coverage
    // conditions; diagnostic callers re-enable it explicitly.
    bool tangentCoarseAlignmentEnabled = false;
    bool tangentCoarseAlignmentAuto = true;
    // When true (compatible default) the registration result carries the 2 mm
    // pre/post ICP diagnostic frames; callers in standard output mode disable
    // this to skip the unused diagnostic generation entirely.
    bool collectDiagnosticFrames = true;
    float tangentCoarseSearchRadiusMm = 10.0f;
    float tangentCoarseStepMm = 1.0f;
    float tangentCoarseRefineStepMm = 0.25f;
    float tangentCoarseScoreDistanceMm = 5.0f;
    float tangentCoarseYawRadiusDegrees = 1.0f;
    float tangentCoarseYawStepDegrees = 0.25f;
    // Diagnostic-only by default (balanced mode): the wide structure search
    // stays disabled unless a diagnostic configuration re-enables it.
    bool wideStructureDiagnosticEnabled = false;
    bool applyWideStructureCandidate = false;
    float wideStructureSearchRadiusMm = 120.0f;
    float wideStructureSearchStepMm = 10.0f;
    float wideStructureRefineStepMm = 2.0f;
    float wideStructureYawRadiusDegrees = 3.0f;
    float wideStructureYawStepDegrees = 0.5f;
    float tangentCoarseMinimumCoverageGain = 0.05f;
    CancellationToken cancellation;
    std::function<bool()> isCancelled;
};

inline bool cancellationRequested(const IcpOptions &options)
{
    if (options.cancellation && options.cancellation->load(std::memory_order_relaxed))
        return true;
    return options.isCancelled && options.isCancelled();
}

struct IcpDiagnostics {
    QString filePath;
    int targetCloudIndex = -1;
    int sourceCloudIndex = -1;
    bool attempted = false;
    bool converged = false;
    bool accepted = false;
    int iterations = 0;
    int correspondences = 0;
    int movingSampleCount = 0;
    float fitness = 0.0f;
    float rmse = 0.0f;
    float correctionTranslation = 0.0f;
    float correctionAngleDegrees = 0.0f;
    float overlapRatio = 0.0f;
    float uniqueReferenceRatio = 0.0f;
    float duplicateCorrespondenceRatio = 0.0f;
    float xyRmse = 0.0f;
    float zRmse = 0.0f;
    float correctionX = 0.0f;
    float correctionY = 0.0f;
    float correctionZ = 0.0f;
    int sourceCropCount = 0;
    int targetCropCount = 0;
    int actualOverlapSourceCount = 0;
    int actualOverlapTargetCount = 0;
    int overlapConstrainedStepReductions = 0;
    int safetyConstrainedStepReductions = 0;
    bool stoppedAtSafetyBoundary = false;
    float minimumOverlapRetention = 0.85f;
    bool degenerate = false;
    float conditionRatio = 0.0f;
    int observableDof = 0;
    int projectedIterations = 0;
    float projectedOverlapWidth = 0.0f;
    float nearestDistanceP50 = 0.0f;
    float nearestDistanceP90 = 0.0f;
    float nearestDistanceP95 = 0.0f;
    float coverageWithin3mm = 0.0f;
    float coverageWithin5mm = 0.0f;
    float coverageWithin10mm = 0.0f;
    float nearestDistanceP50AfterPrealignment = 0.0f;
    float nearestDistanceP90AfterPrealignment = 0.0f;
    float nearestDistanceP95AfterPrealignment = 0.0f;
    float coverageWithin3mmAfterPrealignment = 0.0f;
    float coverageWithin5mmAfterPrealignment = 0.0f;
    float coverageWithin10mmAfterPrealignment = 0.0f;
    int structuralSourcePoints = 0;
    int structuralTargetPoints = 0;
    int structuralSourceMatchedPoints = 0;
    int structuralTargetMatchedPoints = 0;
    float structuralSourceToTargetP50 = 0.0f;
    float structuralSourceToTargetP90 = 0.0f;
    float structuralSourceToTargetP95 = 0.0f;
    float structuralTargetToSourceP50 = 0.0f;
    float structuralTargetToSourceP90 = 0.0f;
    float structuralTargetToSourceP95 = 0.0f;
    float structuralSourceToTargetCoverage3mm = 0.0f;
    float structuralSourceToTargetCoverage5mm = 0.0f;
    float structuralSourceToTargetCoverage10mm = 0.0f;
    float structuralTargetToSourceCoverage3mm = 0.0f;
    float structuralTargetToSourceCoverage5mm = 0.0f;
    float structuralTargetToSourceCoverage10mm = 0.0f;
    float structuralBidirectionalCoverage3mm = 0.0f;
    bool structuralValidationPassed = false;
    QString structuralValidationReason;
    bool structuralIcpUsed = false;
    QString structuralIcpReason;
    bool tangentCoarseAlignmentAttempted = false;
    bool tangentCoarseAlignmentApplied = false;
    float tangentCoverageBefore = 0.0f;
    float tangentCoverageAfter = 0.0f;
    float tangentCoarseX = 0.0f;
    float tangentCoarseY = 0.0f;
    float tangentCoarseZ = 0.0f;
    float tangentCoarseYawDegrees = 0.0f;
    QString tangentCoarseAlignmentReason;
    bool planeIdentityTracked = false;
    float trackedSourcePlaneHeight = 0.0f;
    float trackedTargetPlaneHeight = 0.0f;
    QString planeIdentityReason;
    bool wideStructureDiagnosticAttempted = false;
    bool wideStructureDiagnosticValid = false;
    float wideStructureCoverageBefore = 0.0f;
    float wideStructureCoverageBest = 0.0f;
    float wideStructureX = 0.0f;
    float wideStructureY = 0.0f;
    float wideStructureZ = 0.0f;
    float wideStructureYawDegrees = 0.0f;
    QString wideStructureDiagnosticReason;
    bool planeDiagnosticValid = false;
    float planeNormalAngleDegrees = 0.0f;
    float sourcePlaneHeight = 0.0f;
    float targetPlaneHeight = 0.0f;
    float planeOffsetDifference = 0.0f;
    bool planePrealignmentAttempted = false;
    bool planePrealignmentAccepted = false;
    float planePrealignmentTranslation = 0.0f;
    float planePrealignmentAngleDegrees = 0.0f;
    float planeResidualAfterPrealignment = 0.0f;
    float planeNormalAngleAfterPrealignment = 0.0f;
    float planePrealignmentX = 0.0f;
    float planePrealignmentY = 0.0f;
    float planePrealignmentZ = 0.0f;
    QString planePrealignmentReason;
    int completedLevels = 0;
    QVector<float> levelFitness;
    QVector<float> levelRmse;
    QVector<int> levelCorrespondences;
    QString reason;
    int attemptCount = 1;
    QString primaryAttemptReason;
    bool relaxedRetryAttempted = false;
    bool relaxedRetryAccepted = false;
    bool acceptedByRetry = false;
    float initialTranslation = 0.0f;
    float initialAngleDegrees = 0.0f;
    QString relaxedRetryReason;
    float structuralBidirectionalCoverage5mm = 0.0f;
    QString blockingClass;
    int cancellationCheckCount = 0;
    // Per-attempt stage wall-clock timings (ms) of the attempt that produced
    // this diagnostic (the relaxed-retry attempt when relaxedRetryAccepted is
    // set). attemptTotalMs is the full registerPair duration of that attempt.
    qint64 preprocessMs = 0;
    qint64 coarseSearchMs = 0;
    qint64 icpLevel1Ms = 0;
    qint64 icpLevel2Ms = 0;
    qint64 icpLevel3Ms = 0;
    qint64 structuralValidationMs = 0;
    qint64 attemptTotalMs = 0;
    // Total duration (ms) of the first (strict) attempt when a relaxed retry
    // replaced it; 0 otherwise.
    qint64 strictAttemptTotalMs = 0;
    // Sub-stage timings of the attempt that produced this diagnostic.
    qint64 targetLevelCacheMs = 0;   // per-level target downsample+normals+index prep
    qint64 correspondenceMs = 0;     // estimateIncrement calls
    qint64 boundaryCheckMs = 0;      // step-factor overlap boundary trials
    bool targetLevelCacheHit = false; // at least one level reused the cached target artifacts
    // Accepted iterations per level of the final attempt; levelFitness.size().
    QVector<int> levelIterations;
};

struct MultiFrameRegistrationResult {
    struct OverlapDiagnostic {
        int cloudId = -1;
        float intersectionArea = 0.0f;
        float movingArea = 0.0f;
        float unionArea = 0.0f;
        float movingCoverage = 0.0f;
        float intersectionOverUnion = 0.0f;
        bool warning = false;
    };

    QVector<Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    QVector<QString> sourceFiles;
    QVector<FrameTransformMetadata> frameMetadata;
    QVector<IcpDiagnostics> icpDiagnostics;
    QVector<QMatrix4x4> registrationCorrections;
    QVector<float> trackedPlaneHeights;
    bool planeIdentityTrackingValid = false;
    float planeIdentityConsensusHeight = 0.0f;
    QString planeIdentityDiagnostics;
    QVector<QVector<Point3D>> diagnosticPreIcpFrames;
    QVector<QVector<Point3D>> diagnosticPostIcpFrames;
    QVector<OverlapDiagnostic> overlapDiagnostics;
    QString diagnostics;
    qsizetype pointsBeforeCrossCloudFusion = 0;
    qsizetype crossCloudDuplicateCount = 0;
    float crossCloudFusionVoxelSize = 0.0f;
    // Aggregated pipeline stage timings (ms). readMs/transformMs are filled by
    // the caller that owns PLY reading and hand-eye conversion (application
    // wrapper / stitching interface). All other fields are aggregated inside
    // registerRobotBaseFrames over every pair attempt, strict and relaxed.
    qint64 readMs = 0;
    qint64 transformMs = 0;
    qint64 preprocessMs = 0;
    qint64 coarseSearchMs = 0;
    qint64 icpLevel1Ms = 0;
    qint64 icpLevel2Ms = 0;
    qint64 icpLevel3Ms = 0;
    qint64 structuralValidationMs = 0;
    qint64 retryMs = 0;
    qint64 registrationTotalMs = 0;
    qint64 targetLevelCacheMs = 0;   // sum of per-level target prep over attempts
    qint64 correspondenceMs = 0;     // sum of estimateIncrement time over attempts
    qint64 boundaryCheckMs = 0;      // sum of step-factor trial time over attempts
    qint64 diagnosticFrameMs = 0;    // pre/post ICP diagnostic frame generation
    int registrationAttemptCount = 0;
    QString error;
    bool cancelled = false;
    bool ok = false;
};

MultiFrameRegistrationResult registerRobotBaseFrames(
    QVector<RobotBaseFrame> inputs,
    const IcpOptions &icp = {},
    const ProgressCallback &progress = {});

} // namespace pointcloud
