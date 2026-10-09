#include <pcv/registration/multiframe_registration.h>

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QStringList>
#include <QtMath>

#include <cmath>
#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

pointcloud::RobotBaseFrame validFrame(const QString &sourceFile, float offsetX = 0.0f)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    frame.fullPoints.reserve(400);
    frame.samplePoints.reserve(400);
    frame.sourceIndices.reserve(400);
    frame.scanRatios.reserve(400);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const pointcloud::Point3D point{
                offsetX + float(x), float(y), float((x + y) % 3) * 0.25f};
            frame.fullPoints.push_back(point);
            frame.samplePoints.push_back(point);
            frame.sourceIndices.push_back(frame.sourceIndices.size());
            frame.scanRatios.push_back(0.5f);
        }
    }
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setToIdentity();
    return frame;
}

// Dense enough that the intra-pair correspondence search crosses its parallel
// threshold (20 000 moving points) at the 0.5 mm and 0.2 mm levels.
pointcloud::RobotBaseFrame denseFrame(const QString &sourceFile, float offsetX)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    frame.fullPoints.reserve(150 * 150);
    frame.samplePoints.reserve(150 * 150);
    for (int y = 0; y < 150; ++y) {
        for (int x = 0; x < 150; ++x) {
            const pointcloud::Point3D point{
                offsetX + float(x) * 0.5f, float(y) * 0.5f,
                float((x + y) % 3) * 0.25f + float(x % 7) * 0.02f};
            frame.fullPoints.push_back(point);
            frame.samplePoints.push_back(point);
            frame.sourceIndices.push_back(frame.sourceIndices.size());
            frame.scanRatios.push_back(0.5f);
        }
    }
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setToIdentity();
    return frame;
}

pointcloud::IcpOptions simpleIcpOptions()
{
    pointcloud::IcpOptions options;
    options.planePrealignmentEnabled = false;
    options.planeIdentityTrackingEnabled = false;
    options.structuralValidationEnabled = false;
    options.structuralIcpEnabled = false;
    options.tangentCoarseAlignmentEnabled = false;
    options.wideStructureDiagnosticEnabled = false;
    return options;
}

pointcloud::RobotBaseFrame inclinedFrame(const QString &sourceFile, float tiltDegrees,
                                         bool withStructure)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    const float radians = qDegreesToRadians(tiltDegrees);
    const QVector3D u(std::cos(radians), 0.0f, -std::sin(radians));
    const QVector3D v(0.0f, 1.0f, 0.0f);
    const QVector3D n(std::sin(radians), 0.0f, std::cos(radians));
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            QVector3D value = u * (float(x) * 0.5f) + v * (float(y) * 0.5f);
            // An asymmetric raised strip is retained as an observable
            // non-planar feature for steep-plane structural validation.
            if (withStructure && x >= 8 && x < 35 && y >= 10 && y < 55)
                value += n * 8.0f;
            const pointcloud::Point3D point{value.x(), value.y(), value.z()};
            frame.fullPoints.push_back(point);
            frame.samplePoints.push_back(point);
            frame.sourceIndices.push_back(frame.sourceIndices.size());
            frame.scanRatios.push_back(0.5f);
        }
    }
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setToIdentity();
    return frame;
}

pointcloud::IcpOptions steepPlaneOptions()
{
    pointcloud::IcpOptions options;
    options.planePrealignmentEnabled = false;
    options.planeIdentityTrackingEnabled = false;
    options.tangentCoarseAlignmentEnabled = false;
    options.tangentCoarseAlignmentAuto = false;
    options.wideStructureDiagnosticEnabled = false;
    options.parallelEnabled = false;
    return options;
}

// View-dependent dropout fixture for the structural acceptance tests.
//
// It reproduces what a steep face does inside the overlap box: a horizontal
// base plane carries one island that both frames observe and a second island
// that only the moving frame observes, because the reference frame has a hole
// there (a shadowed region it never sampled). Every point of that island is
// farther than the 15 mm correspondence radius from every reference point, so
// it stays unmatched, while the shared island matches exactly. The metrics
// below are therefore a property of what each view observed, not of ICP
// convergence.
// registerRobotBaseFrames takes the reference frame first and the moving frame
// second, so the struct keeps those two roles explicit: the reference only ever
// observed the shared island (it has a hole where the other view saw more).
struct DropoutPair {
    pointcloud::RobotBaseFrame reference;
    pointcloud::RobotBaseFrame moving;
};

// Deterministic, position-keyed micro-roughness (+/-0.25 mm). Both frames apply
// the same offset at the same physical point, so a correct alignment still
// matches exactly, while an incorrect offset lands between samples. Without it
// the synthetic 0.5 mm grid is perfectly periodic and any grid-aligned
// translation scores exactly zero, which real scanned surfaces never do.
QVector3D positionJitter(float x, float y, float z)
{
    const qint64 key = qint64(std::lround(double(x) * 1000.0)) * 73856093LL
        + qint64(std::lround(double(y) * 1000.0)) * 19349663LL
        + qint64(std::lround(double(z) * 1000.0)) * 83492791LL;
    const auto channel = [key](int index) {
        quint32 state = quint32(key + qint64(index) * 2654435761LL) + 1013904223u;
        state ^= state >> 13;
        state *= 2246822519u;
        state ^= state >> 17;
        return float(double(state % 2001u) / 1000.0) - 1.0f;
    };
    return {channel(0) * 0.25f, channel(1) * 0.25f, channel(2) * 0.25f};
}

void appendDropoutPoint(pointcloud::RobotBaseFrame *frame, float x, float y, float z)
{
    const QVector3D jitter = positionJitter(x, y, z);
    const pointcloud::Point3D point{x + jitter.x(), y + jitter.y(), z + jitter.z()};
    frame->fullPoints.push_back(point);
    frame->samplePoints.push_back(point);
    frame->sourceIndices.push_back(frame->sourceIndices.size());
    frame->scanRatios.push_back(0.5f);
}

DropoutPair dropoutPair()
{
    constexpr float basePitch = 1.0f;
    constexpr float islandPitch = 0.5f;
    constexpr float baseMaxX = 70.0f;
    constexpr float baseMaxY = 46.0f;
    constexpr float islandHeight = 12.0f;
    constexpr float sharedMinX = 4.0f;
    constexpr float sharedMaxX = 20.0f;
    constexpr float islandMinY = 13.0f;
    constexpr float islandMaxY = 29.0f;
    constexpr float extraMinX = 40.0f;
    constexpr float extraMaxX = 56.0f;
    // The target hole covers the source-only island plus at least 12 mm of
    // clearance on every side, so the closest target point to it is still more
    // than 15 mm away even though the island only rises 12 mm.
    constexpr float holeMinX = 26.0f;
    constexpr float holeMaxX = 68.0f;
    constexpr float holeMinY = 1.0f;
    constexpr float holeMaxY = 45.0f;

    DropoutPair pair;
    pair.reference.sourceFile = QStringLiteral("dropout_reference.ply");
    pair.moving.sourceFile = QStringLiteral("dropout_moving.ply");
    for (float x = 0.0f; x < baseMaxX; x += basePitch) {
        for (float y = 0.0f; y < baseMaxY; y += basePitch) {
            appendDropoutPoint(&pair.moving, x, y, 0.0f);
            const bool insideHole = x >= holeMinX && x < holeMaxX
                && y >= holeMinY && y < holeMaxY;
            if (!insideHole) appendDropoutPoint(&pair.reference, x, y, 0.0f);
        }
    }
    for (float x = sharedMinX; x < sharedMaxX; x += islandPitch) {
        for (float y = islandMinY; y < islandMaxY; y += islandPitch) {
            appendDropoutPoint(&pair.moving, x, y, islandHeight);
            appendDropoutPoint(&pair.reference, x, y, islandHeight);
        }
    }
    for (float x = extraMinX; x < extraMaxX; x += islandPitch)
        for (float y = islandMinY; y < islandMaxY; y += islandPitch)
            appendDropoutPoint(&pair.moving, x, y, islandHeight);

    for (pointcloud::RobotBaseFrame *frame : {&pair.moving, &pair.reference}) {
        frame->declaredCount = frame->fullPoints.size();
        frame->startBaseFromFlange.setToIdentity();
        frame->endBaseFromFlange.setToIdentity();
    }
    return pair;
}

pointcloud::IcpOptions dropoutOptions()
{
    pointcloud::IcpOptions options;
    options.planePrealignmentEnabled = false;
    options.planeIdentityTrackingEnabled = false;
    options.tangentCoarseAlignmentEnabled = false;
    options.tangentCoarseAlignmentAuto = false;
    options.wideStructureDiagnosticEnabled = false;
    options.parallelEnabled = false;
    // The fixture is deliberately small: this test exercises the evidence
    // floor and the matched-point denominator, not the production threshold.
    options.minimumStructuralPoints = 50;
    return options;
}

// A shallow support plane carrying one steep (40 degree) structural face: the
// geometry that routed the crop frame wrongly on the real pairs. The main plane
// still looks horizontal, so a rule based on its tilt alone keeps the fixed
// robot-XYZ box even though the steep face bounds the pair.
pointcloud::RobotBaseFrame shallowBaseSteepStructureFrame(const QString &sourceFile)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    for (float x = 0.0f; x < 60.0f; x += 1.0f)
        for (float y = 0.0f; y < 40.0f; y += 1.0f)
            appendDropoutPoint(&frame, x, y, 0.0f);
    const float slope = std::tan(qDegreesToRadians(40.0f));
    for (float x = 20.0f; x < 32.0f; x += 0.5f)
        for (float y = 14.0f; y < 26.0f; y += 0.5f)
            appendDropoutPoint(&frame, x, y, 4.0f + (x - 20.0f) * slope);
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setToIdentity();
    return frame;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    {
        auto referenceOnly = validFrame(QStringLiteral("reference.ply"));
        auto second = validFrame(QStringLiteral("second.ply"));
        pointcloud::IcpOptions options = simpleIcpOptions();
        options.enabled = false;
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(referenceOnly), std::move(second)}, options);
        if (!require(result.ok, "reference-only registration must succeed")) return 1;
        if (!require(result.icpDiagnostics.size() == 2,
                     "reference-only registration must report both frames")) return 1;
        if (!require(result.icpDiagnostics[0].reason == QString::fromUtf8("参考点云"),
                     "reference diagnostic reason mismatch")) return 1;
    }

    {
        auto first = validFrame(QStringLiteral("first.ply"));
        auto second = validFrame(QStringLiteral("second.ply"));
        pointcloud::IcpOptions options = simpleIcpOptions();
        QStringList progressMessages;
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(first), std::move(second)}, options,
            [&progressMessages](float, const QString &message) {
                progressMessages.push_back(message);
            });
        if (!require(!progressMessages.isEmpty(),
                     "ICP registration must report progress before pair processing")) return 1;
        bool sawPairMessage = false;
        for (const QString &message : progressMessages) {
            if (message.contains(QString::fromUtf8("配准 scan 2 到相邻点云 scan 1"))) {
                sawPairMessage = true;
                break;
            }
        }
        if (!require(sawPairMessage, "ICP pair progress message was not emitted")) return 1;
        if (!require(result.icpDiagnostics.size() >= 2,
                     "ICP registration must report the reference and pair diagnostics")) return 1;
        if (!require(result.icpDiagnostics[1].attemptCount == 1,
                     "successful pair should report one attempt")) return 1;
        if (!require(result.icpDiagnostics[1].primaryAttemptReason
                         == result.icpDiagnostics[1].reason,
                     "primary attempt reason must match the final reason without retry")) return 1;
    }

    {
        const auto empty = pointcloud::registerRobotBaseFrames({});
        if (!require(!empty.ok && !empty.error.isEmpty(),
                     "empty registration input must be rejected")) return 1;
    }

    {
        auto malformed = validFrame(QStringLiteral("malformed.ply"));
        malformed.scanRatios.removeLast();
        const auto result = pointcloud::registerRobotBaseFrames(
            {std::move(malformed), validFrame(QStringLiteral("second.ply"))});
        if (!require(!result.ok && result.error.contains(QStringLiteral("长度不一致")),
                     "malformed source mapping must be rejected")) return 1;
    }

    {
        pointcloud::IcpOptions options = simpleIcpOptions();
        options.enabled = false;
        options.isCancelled = [] { return true; };
        const auto result = pointcloud::registerRobotBaseFrames(
            {validFrame(QStringLiteral("cancelled.ply"))}, options);
        if (!require(!result.ok && result.cancelled,
                     "cancelled registration must report cancellation")) return 1;
    }

    {
        pointcloud::IcpOptions options = simpleIcpOptions();
        options.enabled = false;
        auto token = std::make_shared<std::atomic_bool>(false);
        options.cancellation = token;
        const auto clean = pointcloud::registerRobotBaseFrames(
            {validFrame(QStringLiteral("token_clean.ply"))}, options);
        if (!require(clean.ok && !clean.cancelled,
                     "an unset token must not cancel registration")) return 1;
        token->store(true);
        const auto cancelledByToken = pointcloud::registerRobotBaseFrames(
            {validFrame(QStringLiteral("token_cancelled.ply"))}, options);
        if (!require(!cancelledByToken.ok && cancelledByToken.cancelled,
                     "a raised token must cancel registration")) return 1;
    }

    for (const float tilt : {60.0f, 75.0f}) {
        pointcloud::IcpOptions options = steepPlaneOptions();
        const auto result = pointcloud::registerRobotBaseFrames(
            {inclinedFrame(QStringLiteral("steep_feature_reference.ply"), tilt, true),
             inclinedFrame(QStringLiteral("steep_feature_source.ply"), tilt, true)}, options);
        if (!require(result.icpDiagnostics.size() == 2,
                     "steep feature pair must report two diagnostics")) return 1;
        const auto &diagnostic = result.icpDiagnostics[1];
        if (!require(result.ok && diagnostic.accepted,
                     "steep feature plane must satisfy the normal registration acceptance")) return 1;
        if (!require(diagnostic.steepPlaneDetected
                         && diagnostic.planeFitMethod == QStringLiteral("ransac_pca"),
                     "steep feature plane must use direction-independent RANSAC/PCA fitting")) return 1;
        if (!require(diagnostic.overlapCoordinateSystem == QStringLiteral("plane_uvn"),
                     "steep feature plane must use local plane overlap coordinates")) return 1;
        if (!require(diagnostic.completedLevels == 3
                         && diagnostic.structuralBidirectionalCoverage3mm >= 0.65f,
                     "steep feature plane must complete all ICP levels and retain 3mm coverage")) return 1;
    }

    {
        pointcloud::IcpOptions options = steepPlaneOptions();
        const auto result = pointcloud::registerRobotBaseFrames(
            {inclinedFrame(QStringLiteral("over_limit_reference.ply"), 76.0f, true),
             inclinedFrame(QStringLiteral("over_limit_source.ply"), 76.0f, true)}, options);
        if (!require(result.icpDiagnostics.size() == 2
                         && !result.icpDiagnostics[1].accepted
                         && result.icpDiagnostics[1].reason == QStringLiteral("plane_tilt_exceeds_limit"),
                     "planes over 75 degrees must be rejected with plane_tilt_exceeds_limit")) return 1;
    }

    {
        pointcloud::IcpOptions options = steepPlaneOptions();
        const auto result = pointcloud::registerRobotBaseFrames(
            {inclinedFrame(QStringLiteral("steep_plain_reference.ply"), 60.0f, false),
             inclinedFrame(QStringLiteral("steep_plain_source.ply"), 60.0f, false)}, options);
        if (!require(result.icpDiagnostics.size() == 2
                         && !result.icpDiagnostics[1].accepted
                         && result.icpDiagnostics[1].reason.contains(QStringLiteral("steep_plane_feature_insufficient")),
                     "featureless steep planes must remain fail-closed")) return 1;
    }

    // ---------------------------------------------------------------------
    // WP1: structural acceptance accounting on view-dependent dropout.
    //
    // The target observed only the shared island, so the source-only island is
    // unmatched. The default rule must judge the pair on the points both views
    // observed (matched ratio ~0.5 with ~1.0 accuracy among them -> accepted),
    // while the legacy total-point denominator sees ~0.5 and rejects it.
    // ---------------------------------------------------------------------
    {
        const DropoutPair pair = dropoutPair();
        const auto runPair = [](const DropoutPair &input, pointcloud::IcpOptions options) {
            return pointcloud::registerRobotBaseFrames({input.reference, input.moving}, options);
        };

        const auto matched = runPair(pair, dropoutOptions());
        if (!require(matched.ok && matched.icpDiagnostics.size() == 2,
                     "dropout pair must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &diagnostic = matched.icpDiagnostics[1];
        if (!require(diagnostic.accepted && diagnostic.structuralValidationPassed,
                     "the matched-point rule must accept a well aligned dropout pair")) return 1;
        if (!require(diagnostic.structuralCoverageMode == QStringLiteral("matched_ratio"),
                     "the default coverage mode must be the matched-point ratio")) return 1;
        if (!require(diagnostic.structuralBidirectionalCoverage3mmOfMatched >= 0.9f,
                     "accuracy among matched points must stay high on a dropout pair")) return 1;
        if (!require(diagnostic.structuralBidirectionalCoverage3mm <= 0.7f,
                     "the legacy total-point ratio must stay diluted by the dropout")) return 1;
        if (!require(diagnostic.structuralBidirectionalCoverage3mmOfMatched
                         > diagnostic.structuralBidirectionalCoverage3mm,
                     "the matched denominator must not be confused with the total denominator")) return 1;
        if (!require(diagnostic.structuralSourceMatchRatio > 0.3f
                         && diagnostic.structuralSourceMatchRatio < 0.7f,
                     "the moving matched ratio must reflect the unmatched island")) return 1;
        if (!require(diagnostic.structuralTargetMatchRatio > 0.9f,
                     "the reference structure must be fully observed by the moving frame")) return 1;
        const float expectedSourceRatio = float(diagnostic.structuralSourceMatchedPoints)
            / float(diagnostic.structuralSourcePoints);
        if (!require(std::abs(diagnostic.structuralSourceMatchRatio - expectedSourceRatio) <= 1.0e-3f,
                     "the source matched ratio must equal matched/total")) return 1;
        const int expectedTargetFloor = qMax(50, int(std::ceil(
            double(diagnostic.structuralTargetPoints) * 0.10)));
        if (!require(diagnostic.structuralTargetMatchedEvidenceFloor == expectedTargetFloor,
                     "the evidence floor must be max(minimumStructuralPoints, 10% of the crop)")) return 1;
        if (!require(!diagnostic.visibilityWarning,
                     "a matched ratio above the warning threshold must not warn")) return 1;

        pointcloud::IcpOptions legacyRule = dropoutOptions();
        legacyRule.structuralCoverageOfMatched = false;
        const auto legacy = runPair(pair, legacyRule);
        if (!require(legacy.icpDiagnostics.size() == 2
                         && !legacy.icpDiagnostics[1].accepted,
                     "the legacy total-point rule must reject the same dropout pair")) return 1;
        const pointcloud::IcpDiagnostics &legacyDiagnostic = legacy.icpDiagnostics[1];
        if (!require(legacyDiagnostic.structuralCoverageMode == QStringLiteral("legacy_total"),
                     "the legacy mode must be reported explicitly")) return 1;
        if (!require(legacyDiagnostic.failureCode == QStringLiteral("structural_coverage_insufficient"),
                     "the legacy rejection must keep the coverage failure code")) return 1;
        if (!require(legacyDiagnostic.structuralBidirectionalCoverage3mmOfMatched >= 0.9f,
                     "the matched-point metrics must be reported in legacy mode as well")) return 1;

        pointcloud::IcpOptions raisedFloor = dropoutOptions();
        raisedFloor.minimumStructuralMatchedRatio = 0.6f;
        const auto floorFailure = runPair(pair, raisedFloor);
        if (!require(floorFailure.icpDiagnostics.size() == 2
                         && !floorFailure.icpDiagnostics[1].accepted
                         && floorFailure.icpDiagnostics[1].failureCode
                                == QStringLiteral("structural_points_insufficient"),
                     "a matched ratio below the evidence floor must report insufficient evidence")) return 1;

        pointcloud::IcpOptions relaxedFloor = dropoutOptions();
        relaxedFloor.minimumStructuralMatchedRatio = 0.3f;
        const auto floorPass = runPair(pair, relaxedFloor);
        if (!require(floorPass.icpDiagnostics.size() == 2
                         && floorPass.icpDiagnostics[1].accepted,
                     "a matched ratio above the evidence floor must be judged on accuracy")) return 1;

        pointcloud::IcpOptions warning = dropoutOptions();
        warning.visibilityWarningRatio = 0.6f;
        const auto warned = runPair(pair, warning);
        if (!require(warned.icpDiagnostics.size() == 2
                         && warned.icpDiagnostics[1].accepted
                         && warned.icpDiagnostics[1].visibilityWarning,
                     "a low matched ratio must warn without rejecting the pair")) return 1;
    }

    // ---------------------------------------------------------------------
    // WP2: the coarse in-plane search must see a correction the legacy
    // coverage score is blind to.
    //
    // The moving cloud is displaced by 4 mm along y, i.e. inside the 5 mm
    // scoring radius of translatedCoverage: the legacy score is almost the same
    // before and after the correction, so it refuses to apply it. That is the
    // mechanism behind the 5_40 pair, where a real ~8 mm correction only moved
    // coverage from 0.150 to 0.166 and was discarded. The trimmed
    // nearest-neighbour objective measures the actual distance and applies it.
    // ---------------------------------------------------------------------
    {
        DropoutPair shifted = dropoutPair();
        for (pointcloud::Point3D &point : shifted.moving.fullPoints) point.y += 4.0f;
        for (pointcloud::Point3D &point : shifted.moving.samplePoints) point.y += 4.0f;
        const auto runPair = [](const DropoutPair &input, pointcloud::IcpOptions options) {
            return pointcloud::registerRobotBaseFrames({input.reference, input.moving}, options);
        };

        // The 4 mm coarse search sample of these small islands holds a few tens
        // of points, so the sample floor is scaled to the fixture as well.
        // The normal gate and the structural routing are opt-in features, so the
        // tests enable them explicitly rather than relying on a default.
        const auto coarseOptions = [&]() {
            pointcloud::IcpOptions options = dropoutOptions();
            options.minimumStructuralPoints = 10;
            options.tangentCoarseAlignmentEnabled = true;
            options.normalCompatibilityDegrees = 45.0f;
            return options;
        };

        pointcloud::IcpOptions legacyCoarse = coarseOptions();
        legacyCoarse.tangentCoarseTrimmedScore = false;
        const auto legacy = runPair(shifted, legacyCoarse);
        if (!require(legacy.icpDiagnostics.size() == 2,
                     "the shifted pair must report two diagnostics")) return 1;
        if (!require(legacy.icpDiagnostics[1].tangentCoarseAlignmentAttempted
                         && !legacy.icpDiagnostics[1].tangentCoarseAlignmentApplied,
                     "the legacy coverage score must stay blind to a 4 mm in-plane offset")) return 1;
        if (!require(legacy.icpDiagnostics[1].tangentCoarseScoreMode
                         == QStringLiteral("legacy_coverage"),
                     "the legacy coarse mode must be reported explicitly")) return 1;

        const auto trimmed = runPair(shifted, coarseOptions());
        if (!require(trimmed.icpDiagnostics.size() == 2,
                     "the shifted pair must report two diagnostics in trimmed mode")) return 1;
        const pointcloud::IcpDiagnostics &trimmedDiagnostic = trimmed.icpDiagnostics[1];
        if (!require(trimmedDiagnostic.tangentCoarseScoreMode == QStringLiteral("trimmed_nearest"),
                     "the trimmed coarse mode must be reported explicitly")) return 1;
        if (!require(trimmedDiagnostic.tangentCoarseAlignmentApplied,
                     "the trimmed objective must apply the 4 mm in-plane correction")) return 1;
        if (!require(std::abs(trimmedDiagnostic.tangentCoarseY + 4.0f) <= 1.0f,
                     "the trimmed search must recover the in-plane offset")) return 1;
        if (!require(trimmedDiagnostic.tangentCoarseScoreAfter
                         < trimmedDiagnostic.tangentCoarseScoreBefore,
                     "the trimmed objective must improve on the recovered offset")) return 1;
        if (!require(trimmedDiagnostic.accepted
                         && trimmedDiagnostic.structuralBidirectionalCoverage3mmOfMatched >= 0.9f,
                     "the recovered pair must satisfy the matched-point acceptance")) return 1;

        // The normal-compatibility gate stays compatible with an unobstructed
        // pair: enabling it must not move the accepted correction.
        // Both the normal gate and the structural routing are opt-in, so the
        // test enables them explicitly instead of relying on a default.
        pointcloud::IcpOptions gateOff = coarseOptions();
        gateOff.normalCompatibilityDegrees = 0.0f;
        const auto withoutGate = runPair(shifted, gateOff);
        if (!require(withoutGate.icpDiagnostics.size() == 2
                         && withoutGate.icpDiagnostics[1].accepted,
                     "the pair must still register with the normal gate disabled")) return 1;
        // The gate removes correspondences, so it may retune the correction;
        // what it must never do is lose an already recovered alignment.
        if (!require(withoutGate.icpDiagnostics[1].accepted
                         && withoutGate.icpDiagnostics[1].structuralBidirectionalCoverage3mmOfMatched
                                >= 0.9f,
                     "the pair must still register with the normal gate disabled")) return 1;
        if (!require(std::abs(withoutGate.icpDiagnostics[1].correctionY + 4.0f) <= 0.5f
                         && std::abs(withoutGate.icpDiagnostics[1].correctionX) <= 0.5f,
                     "the gate-disabled pair must recover the same offset")) return 1;
        // Estimated normals on a jittered surface already disagree by more than
        // 45 degrees at the finest level, so the gate is expected to reject a
        // few correspondences there; the counter proves it is wired in, and the
        // A/B above proves it does not lose the alignment.
        if (!require(trimmedDiagnostic.normalRejectedCorrespondences > 0,
                     "the normal gate must report the correspondences it rejects")) return 1;
        if (!require(withoutGate.icpDiagnostics[1].normalRejectedCorrespondences == 0,
                     "a disabled normal gate must reject nothing")) return 1;
    }

    // ---------------------------------------------------------------------
    // WP3: the overlap frame follows the structure, not only the main plane.
    //
    // The base plane is shallow (the fitted main plane is horizontal), yet the
    // pair is bounded by a 40 degree face sitting on it. Routing by the main
    // plane tilt alone keeps the robot-XYZ box, which is exactly what the real
    // steep pairs did; the structural routing switches to the structure's own
    // frame.
    // ---------------------------------------------------------------------
    {
        const auto makeFrames = []() {
            return QVector<pointcloud::RobotBaseFrame>{
                shallowBaseSteepStructureFrame(QStringLiteral("route_reference.ply")),
                shallowBaseSteepStructureFrame(QStringLiteral("route_moving.ply"))};
        };

        pointcloud::IcpOptions structuralRouting = dropoutOptions();
        structuralRouting.structuralFrameRouteDegrees = 30.0f;
        const auto routed = pointcloud::registerRobotBaseFrames(makeFrames(), structuralRouting);
        if (!require(routed.icpDiagnostics.size() == 2,
                     "the routing fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &routedDiagnostic = routed.icpDiagnostics[1];
        if (!require(!routedDiagnostic.steepPlaneDetected,
                     "the main plane of the routing fixture must stay shallow")) return 1;
        if (!require(routedDiagnostic.overlapCoordinateSystem == QStringLiteral("plane_uvn"),
                     "the dominant structure must define the overlap frame")) return 1;
        if (!require(routedDiagnostic.accepted,
                     "the structurally routed pair must still register")) return 1;

        pointcloud::IcpOptions legacyRouting = dropoutOptions();
        legacyRouting.structuralFrameRouteDegrees = 90.0f;
        const auto legacy = pointcloud::registerRobotBaseFrames(makeFrames(), legacyRouting);
        if (!require(legacy.icpDiagnostics.size() == 2,
                     "the routing fixture must report two diagnostics in legacy mode")) return 1;
        if (!require(legacy.icpDiagnostics[1].overlapCoordinateSystem == QStringLiteral("robot_xyz"),
                     "disabling the structural routing must restore the robot-XYZ frame")) return 1;
    }

    // ---------------------------------------------------------------------
    // WP5: the intra-pair correspondence search must be chunk-parallel and
    // exactly equivalent to the serial search.
    //
    // The per-pair loop stays serial and in scan order; only the search inside
    // one iteration is split. Chunks are merged in index order, so the pair
    // order - and with it the accumulated normal equations - is identical.
    // ---------------------------------------------------------------------
    {
        const auto makeFrames = []() {
            return QVector<pointcloud::RobotBaseFrame>{
                denseFrame(QStringLiteral("dense_reference.ply"), 0.0f),
                denseFrame(QStringLiteral("dense_moving.ply"), 2.0f)};
        };

        pointcloud::IcpOptions serialOptions = simpleIcpOptions();
        serialOptions.parallelEnabled = false;
        // Keep an extra shared copy so the frame buffers have refcount > 1 while
        // the chunk workers read them.
        QVector<pointcloud::RobotBaseFrame> serialInput = makeFrames();
        const QVector<pointcloud::RobotBaseFrame> retained = serialInput;
        const auto serial = pointcloud::registerRobotBaseFrames(std::move(serialInput), serialOptions);
        if (!require(serial.ok && serial.icpDiagnostics.size() == 2,
                     "the dense pair must register serially")) return 1;
        if (!require(serial.icpDiagnostics[1].icpCorrespondenceParallelTasks == 1,
                     "the serial run must report one correspondence worker")) return 1;

        for (const int tasks : {2, 4}) {
            pointcloud::IcpOptions options = simpleIcpOptions();
            options.parallelEnabled = true;
            options.correspondenceParallelTasks = tasks;
            const auto parallel = pointcloud::registerRobotBaseFrames(makeFrames(), options);
            if (!require(parallel.ok && parallel.icpDiagnostics.size() == 2,
                         "the dense pair must register with chunked correspondence search")) return 1;
            const pointcloud::IcpDiagnostics &expected = serial.icpDiagnostics[1];
            const pointcloud::IcpDiagnostics &actual = parallel.icpDiagnostics[1];
            if (!require(actual.icpCorrespondenceParallelTasks == tasks,
                         "the chunked run must report the requested worker count")) return 1;
            if (!require(actual.accepted == expected.accepted
                             && actual.structuralSourceMatchedPoints == expected.structuralSourceMatchedPoints
                             && actual.structuralTargetMatchedPoints == expected.structuralTargetMatchedPoints
                             && actual.correspondences == expected.correspondences
                             && actual.completedLevels == expected.completedLevels,
                         "chunked correspondence search must keep the accept decision and counts")) return 1;
            if (!require(actual.correctionTranslation == expected.correctionTranslation
                             && actual.correctionAngleDegrees == expected.correctionAngleDegrees,
                         "chunked correspondence search must keep the correction")) return 1;
            for (int element = 0; element < 16; ++element) {
                if (!require(serial.registrationCorrections[1].constData()[element]
                                 == parallel.registrationCorrections[1].constData()[element],
                             "chunked correspondence search must keep the correction matrix")) return 1;
            }
            if (!require(parallel.points.size() == serial.points.size()
                             && parallel.cloudIds == serial.cloudIds
                             && parallel.sourceIndices == serial.sourceIndices,
                         "chunked correspondence search must keep the source mapping")) return 1;
        }

        if (!require(retained.size() == 2
                         && retained.first().fullPoints.size() == 150 * 150,
                     "chunked correspondence search must not mutate the shared input")) return 1;
    }

    // ---------------------------------------------------------------------
    // Parallel overlap precomputation regression.
    //
    // The overlap pre-pass runs one worker per neighbouring pair over the
    // shared QVector<ConvertedCloud>. It must stay a pure const read: every
    // mode has to produce identical overlaps, points, source mapping and
    // corrections, and the ICP commit loop stays strictly in scan order.
    // ---------------------------------------------------------------------
    {
        const auto makeFrames = [](int count) {
            QVector<pointcloud::RobotBaseFrame> inputs;
            for (int index = 0; index < count; ++index)
                inputs.push_back(validFrame(
                    QStringLiteral("parallel_pair_%1.ply").arg(index), 2.0f * float(index)));
            return inputs;
        };

        pointcloud::IcpOptions serialOptions = simpleIcpOptions();
        serialOptions.parallelEnabled = false;
        QVector<pointcloud::RobotBaseFrame> serialInput = makeFrames(4);
        // Retain an extra shared copy of the inputs so their Qt buffers have
        // refcount > 1 while registration runs.
        const QVector<pointcloud::RobotBaseFrame> retainedInput = serialInput;
        const pointcloud::Point3D referencePoint = retainedInput.first().fullPoints.first();
        const auto serialResult =
            pointcloud::registerRobotBaseFrames(std::move(serialInput), serialOptions);
        if (!require(!serialResult.parallelEnabled
                     && serialResult.overlapDiagnostics.size() == 3,
                     "serial registration must precompute one overlap per pair")) return 1;

        for (const int tasks : {1, 2, 4}) {
            pointcloud::IcpOptions options = simpleIcpOptions();
            options.parallelEnabled = true;
            options.maxParallelTasks = tasks;
            const auto parallelResult =
                pointcloud::registerRobotBaseFrames(makeFrames(4), options);
            if (!require(parallelResult.parallelEnabled == (tasks > 1),
                         "registration must honour the requested parallel task count")) return 1;
            if (!require(parallelResult.overlapDiagnostics.size()
                             == serialResult.overlapDiagnostics.size(),
                         "parallel overlap count must match the serial run")) return 1;
            for (qsizetype index = 0; index < serialResult.overlapDiagnostics.size(); ++index) {
                const pointcloud::MultiFrameRegistrationResult::OverlapDiagnostic &expected =
                    serialResult.overlapDiagnostics[index];
                const pointcloud::MultiFrameRegistrationResult::OverlapDiagnostic &actual =
                    parallelResult.overlapDiagnostics[index];
                if (!require(expected.cloudId == actual.cloudId
                             && expected.intersectionArea == actual.intersectionArea
                             && expected.movingArea == actual.movingArea
                             && expected.unionArea == actual.unionArea
                             && expected.movingCoverage == actual.movingCoverage
                             && expected.intersectionOverUnion == actual.intersectionOverUnion
                             && expected.warning == actual.warning,
                             "parallel overlap diagnostics must match the serial run")) return 1;
            }
            if (!require(parallelResult.points.size() == serialResult.points.size()
                         && parallelResult.cloudIds == serialResult.cloudIds
                         && parallelResult.sourceIndices == serialResult.sourceIndices
                         && parallelResult.scanRatios == serialResult.scanRatios,
                         "parallel registration must match the serial source mapping")) return 1;
            for (qsizetype index = 0; index < serialResult.points.size(); ++index) {
                if (!require(parallelResult.points[index].x == serialResult.points[index].x
                             && parallelResult.points[index].y == serialResult.points[index].y
                             && parallelResult.points[index].z == serialResult.points[index].z,
                             "parallel registration must keep point order")) return 1;
            }
            if (!require(parallelResult.registrationCorrections.size()
                             == serialResult.registrationCorrections.size(),
                         "parallel registration must keep the correction count")) return 1;
            for (qsizetype index = 0; index < serialResult.registrationCorrections.size(); ++index) {
                const QMatrix4x4 &expected = serialResult.registrationCorrections[index];
                const QMatrix4x4 &actual = parallelResult.registrationCorrections[index];
                for (int element = 0; element < 16; ++element) {
                    if (!require(expected.constData()[element] == actual.constData()[element],
                                 "parallel registration must keep the correction matrices")) return 1;
                }
            }
            if (!require(parallelResult.icpDiagnostics.size()
                             == serialResult.icpDiagnostics.size(),
                         "parallel registration must keep the ICP diagnostic count")) return 1;
        }

        // The retained shared copy must still describe the untouched input.
        if (!require(retainedInput.size() == 4
                     && retainedInput.first().fullPoints.first().x == referencePoint.x
                     && retainedInput.first().fullPoints.first().y == referencePoint.y
                     && retainedInput.first().fullPoints.first().z == referencePoint.z,
                     "registration must not mutate the shared input frames")) return 1;
    }

    // 30 degree inclined pair: success or a structured rejection, never an
    // abnormal exit, in both serial and parallel modes.
    for (const bool parallel : {false, true}) {
        pointcloud::IcpOptions options = steepPlaneOptions();
        options.parallelEnabled = parallel;
        options.maxParallelTasks = parallel ? 4 : 0;
        const auto result = pointcloud::registerRobotBaseFrames(
            {inclinedFrame(QStringLiteral("tilt30_reference.ply"), 30.0f, true),
             inclinedFrame(QStringLiteral("tilt30_source.ply"), 30.0f, true)}, options);
        if (!require(!result.cancelled,
                     "a 30 degree pair must never report cancellation")) return 1;
        if (!require(result.ok ? (!result.points.isEmpty()
                                  && result.icpDiagnostics.size() == 2)
                               : !result.error.isEmpty(),
                     "a 30 degree pair must succeed or fail in a structured way")) return 1;
    }

    std::cout << "Multiframe registration tests passed.\n";
    return 0;
}
