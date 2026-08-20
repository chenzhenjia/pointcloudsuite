#pragma once

#include <pcv/core/point_types.h>

#include <QMatrix4x4>
#include <QString>
#include <QVector>

#include <functional>

namespace pointcloud {

using ProgressCallback = std::function<void(float, const QString &)>;

struct WorldCloudInput {
    QString filePath;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    QMatrix4x4 flangeFromDepth;
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
    int minimumStructuralPoints = 100;
    bool structuralIcpEnabled = true;
    int supportPlaneSampleStride = 16;
    bool tangentCoarseAlignmentEnabled = true;
    float tangentCoarseSearchRadiusMm = 10.0f;
    float tangentCoarseStepMm = 1.0f;
    float tangentCoarseRefineStepMm = 0.25f;
    float tangentCoarseScoreDistanceMm = 5.0f;
    float tangentCoarseYawRadiusDegrees = 1.0f;
    float tangentCoarseYawStepDegrees = 0.25f;
    bool wideStructureDiagnosticEnabled = true;
    float wideStructureSearchRadiusMm = 120.0f;
    float wideStructureSearchStepMm = 10.0f;
    float wideStructureRefineStepMm = 2.0f;
    float wideStructureYawRadiusDegrees = 3.0f;
    float wideStructureYawStepDegrees = 0.5f;
    float tangentCoarseMinimumCoverageGain = 0.05f;
    std::function<bool()> isCancelled;
};

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
};

struct WorldCloudMergeResult {
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
    QString error;
    bool cancelled = false;
    bool ok = false;
};

WorldCloudMergeResult mergePlyCloudsInWorld(
    const QVector<WorldCloudInput> &inputs,
    const IcpOptions &icp = {},
    const ProgressCallback &progress = {});

} // namespace pointcloud
