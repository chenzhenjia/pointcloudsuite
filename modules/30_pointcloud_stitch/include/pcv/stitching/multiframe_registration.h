#pragma once

#include <pcv/core/cancellation.h>
#include <pcv/core/point_types.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

namespace pointcloud {

enum class RegistrationProfile { Legacy, PoseOverlapFine };

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
    float overlapMargin = 0.0f;
    bool planePrealignmentEnabled = false;
    float maximumPlanePrealignmentTranslation = 6.0f;
    float maximumPlanePrealignmentAngleDegrees = 0.5f;
    bool planeIdentityTrackingEnabled = true;
    float planeIdentityHeightToleranceMm = 5.0f;
    float planeFitHeightBandMm = 3.0f;
    float planeFitInlierDistanceMm = 0.8f;
    int planeFitIterations = 3;
    // Direction-independent fallback for planes whose normal is not close to
    // the robot-base Z axis.  The legacy height-band path remains the default
    // for shallow planes to preserve existing results.
    float maximumPlaneTiltDegrees = 75.0f;
    int planeFitRansacHypotheses = 256;
    float minimumPlaneInlierRatio = 0.15f;
    float planeIdentityNormalToleranceDegrees = 3.0f;
    float planeIdentityOffsetToleranceMm = 5.0f;
    int steepPlaneMinimumObservableDof = 3;
    float maximumCorrectionTranslation = 2.0f;
    float maximumCorrectionAngleDegrees = 0.2f;
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
    bool tangentCoarseAlignmentAuto = false;
    // When true (compatible default) the registration result carries the 2 mm
    // pre/post ICP diagnostic frames; callers in standard output mode disable
    // this to skip the unused diagnostic generation entirely.
    bool collectDiagnosticFrames = true;
    bool parallelEnabled = true;
    int maxParallelTasks = 0;
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

    // Tuning options appended after every pre-existing member so that each
    // legacy field keeps its offset (see the 2026-09-15 ABI note in
    // docs/requirements/pointcloudview_v0.3.md).
    // When true (default) the structural acceptance ratio is measured among
    // the points that actually found a counterpart in the other cloud
    // ("matched"). The legacy total-point denominators stay computed and are
    // still reported; structuralCoverageOfMatched=false restores the legacy
    // acceptance rule exactly.
    bool structuralCoverageOfMatched = true;
    // Evidence floor: at least this share of the cropped structural points must
    // have a counterpart on the other side, in both directions, before the
    // accuracy ratio is allowed to accept the pair. minimumStructuralPoints
    // stays as the absolute lower bound, so the effective floor is the maximum
    // of both values.
    float minimumStructuralMatchedRatio = 0.10f;
    // Warn (never reject) when the bidirectional matched ratio drops below this
    // value: the pair is then only verified on a small subset of the crop.
    float visibilityWarningRatio = 0.25f;

    // Coarse in-plane search objective. The legacy bidirectional coverage is
    // diluted by points the other view never observed, so a correct large
    // in-plane correction can score below the old gain gate: on the 5_40 pair
    // the best translation moved coverage from 0.150 to 0.166 while the surface
    // was really off by ~8 mm. With the trimmed objective the search minimises
    // the mean of the K smallest nearest-neighbour distances instead, and
    // tangentCoarseMinimumScoreGain is the relative improvement it must show
    // before the candidate is applied.
    bool tangentCoarseTrimmedScore = true;
    float tangentCoarseTrimmedRatio = 0.25f;
    float tangentCoarseMinimumScoreGain = 0.01f;
    // Reject a correspondence whose source and target normals disagree by more
    // than this angle (0 disables the gate). This removes the across-edge
    // pairings that dominate a tilted surface. OFF by default: on the real d1
    // pair the gate rejected ~913k correspondences and moved the accepted
    // correction by ~1.8 mm against the recorded baseline, which is far outside
    // the agreed drift budget, so it stays an explicit opt-in until it has
    // real-data evidence.
    float normalCompatibilityDegrees = 0.0f;
    // Trimmed ICP weighting: only the best fraction of the residual
    // distribution enters the normal equations. It is OFF by default: on a
    // plane-dominated pair the residual order statistic selects the
    // degenerate support-plane correspondences and discards the informative
    // structure, which measurably pulled the solution away from an already
    // recovered coarse alignment. Enable it explicitly (correspondenceTrimFraction)
    // only for a pair whose overlap is known to be mostly valid.
    bool autoTrimCorrespondences = false;
    float correspondenceTrimFraction = 0.0f;
    // Widen the strict first-level correspondence radius when the trimmed coarse
    // score shows the initial residual is far outside the current basin.
    bool adaptiveCorrespondenceRadius = true;
    float adaptiveCorrespondenceRadiusMm = 4.0f;
    // Intra-pair parallelism for the correspondence search (0 = automatic,
    // bounded by parallelEnabled). The per-pair loop itself stays serial and in
    // scan order; only the search inside one iteration is chunked.
    int correspondenceParallelTasks = 0;
    // Overlap-frame routing: when the dominant structure sitting on the main
    // plane points more than this angle away from the plane normal, that
    // structure defines the local crop frame instead of the fixed robot-XYZ
    // box.
    // OFF by default for the same reason: it changes the crop box on real
    // "shallow main plane + steep structure" data, and no real-data evidence
    // exists yet. 90 disables the structural routing.
    float structuralFrameRouteDegrees = 90.0f;

    // --- 2.5D height-map registration (controlled iteration) ---------------
    // Every field below is appended after every pre-existing member so that no
    // legacy offset moves. The whole path is OFF by default: with
    // heightMapRegistrationEnabled=false the pipeline is byte-for-byte the
    // legacy one, including its formal-output decision.
    bool heightMapRegistrationEnabled = false;
    float heightMapGridMinimumMm = 0.5f;
    float heightMapGridMaximumMm = 2.0f;
    float heightMapMinimumValidRatio = 0.25f;
    int heightMapMinimumValidPoints = 100;
    float heightMapMinimumRelativeImprovement = 0.01f;
    float heightMapMaxTranslationMm = 10.0f;
    float heightMapMaxYawDegrees = 1.0f;
    bool heightMapRestrictedDof = true;
    bool heightMapSearchEnabled = true;
    // Production uses the robot-base pose and overlap as hard constraints.
    // Legacy coarse recovery is available only when this switch is explicit.
    bool recoveryMode = false;
    bool overlapHardConstraint = true;
    float maximumCumulativeCorrectionTranslation = 2.0f;
    float maximumCumulativeCorrectionAngleDegrees = 0.2f;
    float qualityRmsMm = 0.5f;
    float qualityP95Mm = 1.0f;
    float qualityNormalAngleDegrees = 0.5f;
    RegistrationProfile profile = RegistrationProfile::Legacy;
    QVector<float> fineVoxelLevels = {0.5f, 0.2f};
    QVector<float> fineCorrespondenceDistances = {3.0f, 0.6f};
    QVector<int> fineMaximumIterations = {20, 15};
    float fineMaximumTranslationMm = 10.0f;
    float fineMaximumAngleDegrees = 1.0f;
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
    QString planeFitMethod;
    QVector3D sourcePlaneNormal;
    QVector3D targetPlaneNormal;
    float sourcePlaneTiltDegrees = 0.0f;
    float targetPlaneTiltDegrees = 0.0f;
    float sourcePlaneFitRms = 0.0f;
    float targetPlaneFitRms = 0.0f;
    int sourcePlaneInlierCount = 0;
    int targetPlaneInlierCount = 0;
    float sourcePlaneInlierRatio = 0.0f;
    float targetPlaneInlierRatio = 0.0f;
    float planeIdentityNormalDifferenceDegrees = 0.0f;
    float planeIdentityOffsetDifferenceMm = 0.0f;
    bool steepPlaneDetected = false;
    int steepPlaneFeatureCountSource = 0;
    int steepPlaneFeatureCountTarget = 0;
    int steepPlaneObservableDof = 0;
    QString steepPlaneRejectionReason;
    QString overlapCoordinateSystem;
    QVector3D overlapLocalMinimum;
    QVector3D overlapLocalMaximum;
    int localOverlapSourcePoints = 0;
    int localOverlapTargetPoints = 0;
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

    // Structural acceptance accounting (appended for the same ABI reason as
    // IcpOptions above). "Matched" means the point found a counterpart inside
    // the 15 mm search radius of the other side; the OfMatched ratios divide by
    // that subset instead of by every cropped structural point.
    float structuralSourceMatchRatio = 0.0f;
    float structuralTargetMatchRatio = 0.0f;
    float structuralBidirectionalMatchRatio = 0.0f;
    float structuralSourceToTargetCoverage3mmOfMatched = 0.0f;
    float structuralSourceToTargetCoverage5mmOfMatched = 0.0f;
    float structuralTargetToSourceCoverage3mmOfMatched = 0.0f;
    float structuralTargetToSourceCoverage5mmOfMatched = 0.0f;
    float structuralBidirectionalCoverage3mmOfMatched = 0.0f;
    float structuralBidirectionalCoverage5mmOfMatched = 0.0f;
    int structuralSourceMatchedEvidenceFloor = 0;
    int structuralTargetMatchedEvidenceFloor = 0;
    // "matched_ratio" (default) or "legacy_total"; the rule that actually
    // decided this pair, and a snapshot of the thresholds it used.
    QString structuralCoverageMode;
    QString acceptanceRule;
    bool visibilityWarning = false;
    // Machine-readable failure code ("structural_points_insufficient",
    // "structural_coverage_insufficient", "plane_tilt_exceeds_limit", ...).
    // reason keeps the human readable text.
    QString failureCode;

    // Coarse search objective. The trimmed nearest-neighbour score is always
    // reported (smaller is better); the legacy coverage fields above stay
    // comparable with historical reports.
    float tangentCoarseScoreBefore = 0.0f;
    float tangentCoarseScoreAfter = 0.0f;
    QString tangentCoarseScoreMode;
    // Correspondence filtering inside the ICP loop.
    int normalRejectedCorrespondences = 0;
    float correspondenceTrimFractionUsed = 1.0f;
    bool adaptiveCorrespondenceRadiusUsed = false;
    // Chunked correspondence search: how many workers actually ran, and how many
    // of the strict/relaxed attempts skipped the relaxed retry.
    int icpCorrespondenceParallelTasks = 1;
    QString relaxedRetrySkippedReason;
    int skippedLevels = 0;

    // --- 2.5D height-map registration (controlled iteration) ---------------
    // Appended after every pre-existing member (same ABI reason as above).
    // The fixed source vocabulary of selectedRegistrationSource is
    // "legacy_icp", "heightmap_restricted_icp" or "none".
    bool heightMapAttempted = false;
    bool heightMapAccepted = false;
    bool heightMapFallbackToIcp = false;
    float heightMapGridResolutionMm = 0.0f;
    int heightMapWidth = 0;
    int heightMapHeight = 0;
    float heightMapSourceValidRatio = 0.0f;
    float heightMapTargetValidRatio = 0.0f;
    int heightMapSourceValidPoints = 0;
    int heightMapTargetValidPoints = 0;
    float heightMapScoreBefore = 0.0f;
    float heightMapScoreAfter = 0.0f;
    float heightMapRelativeImprovement = 0.0f;
    float heightMapTranslationX = 0.0f;
    float heightMapTranslationY = 0.0f;
    float heightMapTranslationZ = 0.0f;
    float heightMapYawDegrees = 0.0f;
    int heightMapRestrictedIcpCompletedLevels = 0;
    bool heightMapRestrictedIcpDegenerate = false;
    QString heightMapRejectReason;
    QString selectedRegistrationSource;
    bool legacyIcpComparisonAvailable = false;
    float legacyIcpComparisonCoverage3mm = 0.0f;
    float legacyIcpComparisonP95 = 0.0f;
    bool visibilityHardRejected = false;
    // Specific trajectory diagnostic failure (section 8 of the iteration
    // contract) and the legacy baseline outcome of the same pair. The legacy
    // numbers are a comparison only: they never overwrite an accepted 2.5D
    // result.
    QString trajectoryDiagnosticReason;
    bool legacyIcpAccepted = false;
    QString legacyIcpRejectReason;
    float heightMapNormalizedCorrelation = 0.0f;
    // accepted remains the legacy quality verdict; output permission is separate.
    bool fineApplied = false;
    bool poseFallback = false;
    bool chainAnchorReset = false;
    bool structuralMetricsComputed = false;
    QString fineReason;
    QMatrix4x4 pairCorrection;
    QMatrix4x4 cumulativeCorrection;
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
    qint64 conversionWallMs = 0;
    qint64 readWorkMs = 0;
    qint64 transformWorkMs = 0;
    bool parallelEnabled = false;
    int requestedParallelTasks = 0;
    int actualParallelTasks = 1;
    qint64 registrationWorkMs = 0;
    qint64 targetLevelCacheMs = 0;   // sum of per-level target prep over attempts
    qint64 correspondenceMs = 0;     // sum of estimateIncrement time over attempts
    qint64 boundaryCheckMs = 0;      // sum of step-factor trial time over attempts
    qint64 diagnosticFrameMs = 0;    // pre/post ICP diagnostic frame generation
    int registrationAttemptCount = 0;
    QString error;
    bool cancelled = false;
    bool partial = false;
    int completedFrameCount = 0;
    int failedFrameIndex = -1;
    QString registrationSource = QStringLiteral("robot_pose_icp");
    bool ok = false;
};

MultiFrameRegistrationResult registerRobotBaseFrames(
    QVector<RobotBaseFrame> inputs,
    const IcpOptions &icp = {},
    const ProgressCallback &progress = {});

} // namespace pointcloud
