#include <pcv/registration/seam_fusion.h>
#include <pcv/stitching/seam_fusion_kernel.h>

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QtMath>

#include <atomic>
#include <functional>
#include <iostream>

namespace {

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

pointcloud::RobotBaseFrame malformedFrame()
{
    pointcloud::RobotBaseFrame frame;
    frame.fullPoints = {{1.0f, 2.0f, 3.0f}};
    frame.samplePoints = frame.fullPoints;
    frame.scanRatios = {0.0f};
    return frame;
}

// ---------------------------------------------------------------------------
// Parallel seam fusion regression fixtures.
//
// The GUI wrapper (apps/pointcloudstitch/seamfusion.cpp) hands the caller's
// containers to applyTrajectorySeamFusionKernel, and the kernel immediately
// shares them (merge.points = *points). Qt's NON-const operator[] detaches a
// shared buffer, so a worker that indexed merge->points reallocated while its
// peers were still reading the old buffer -- the heap corruption this suite
// guards against. Every fixture below keeps extra shared copies of the input
// vectors alive on purpose, i.e. Qt refcount > 1, so the parallel worker wave
// runs under exactly the condition that used to detach.
// ---------------------------------------------------------------------------

struct ChainFixture {
    QVector<pointcloud::Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    QVector<QMatrix4x4> starts;
    QVector<QMatrix4x4> ends;
};

// Builds frameCount overlapping scans whose centres advance every step mm along
// an axis tilted by tiltDegrees away from +X. Each frame is xSamples *
// ySamples points, so the merged cloud is large enough to split into several
// core chunks.
ChainFixture makeChain(int frameCount, int xSamples, int ySamples,
                       float step, float tiltDegrees, float halfSpan)
{
    ChainFixture fixture;
    const float radians = qDegreesToRadians(tiltDegrees);
    const QVector3D axis(std::cos(radians), 0.0f, std::sin(radians));
    const QVector3D travel(0.0f, 1.0f, 0.0f);
    constexpr float travelSpan = 40.0f;
    for (int frame = 0; frame < frameCount; ++frame) {
        const float frameCenter = float(frame) * step;
        for (int xi = 0; xi < xSamples; ++xi) {
            const float along = frameCenter - halfSpan
                + 2.0f * halfSpan * float(xi) / float(xSamples - 1);
            for (int yi = 0; yi < ySamples; ++yi) {
                const float across = travelSpan * float(yi) / float(ySamples - 1);
                const QVector3D value = axis * along + travel * across;
                fixture.points.push_back({value.x(), value.y(), value.z()});
                fixture.cloudIds.push_back(frame);
                fixture.sourceIndices.push_back(fixture.sourceIndices.size());
                fixture.scanRatios.push_back(0.5f);
            }
        }
        QMatrix4x4 start;
        start.setToIdentity();
        start.translate(axis * frameCenter - travel * (travelSpan * 0.5f));
        QMatrix4x4 end;
        end.setToIdentity();
        end.translate(axis * frameCenter + travel * (travelSpan * 0.5f));
        fixture.starts.push_back(start);
        fixture.ends.push_back(end);
    }
    return fixture;
}

struct FusionOutcome {
    pointcloud::SeamFusionResult result;
    QVector<pointcloud::Point3D> points;
    QVector<int> cloudIds;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
};

// The fixture is taken BY VALUE, so the caller's vectors and this copy already
// share their Qt buffers before the kernel adds another shared reference.
FusionOutcome runFusion(ChainFixture fixture, bool parallel, int tasks, float halfWidth,
                        const std::function<bool()> &isCancelled = {})
{
    QVector<QMatrix4x4> corrections(fixture.starts.size());
    for (QMatrix4x4 &matrix : corrections) matrix.setToIdentity();
    pointcloud::SeamFusionOptions options;
    options.enabled = true;
    options.halfWidth = halfWidth;
    options.parallelEnabled = parallel;
    options.maxParallelTasks = tasks;
    options.isCancelled = isCancelled;
    FusionOutcome outcome;
    outcome.result = pointcloud::applyTrajectorySeamFusionKernel(
        &fixture.points, &fixture.cloudIds, &fixture.sourceIndices, &fixture.scanRatios,
        fixture.starts, fixture.ends, corrections, options);
    outcome.points = fixture.points;
    outcome.cloudIds = fixture.cloudIds;
    outcome.sourceIndices = fixture.sourceIndices;
    outcome.scanRatios = fixture.scanRatios;
    return outcome;
}

bool samePoints(const QVector<pointcloud::Point3D> &a, const QVector<pointcloud::Point3D> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype index = 0; index < a.size(); ++index) {
        if (a[index].x != b[index].x || a[index].y != b[index].y || a[index].z != b[index].z
            || a[index].nx != b[index].nx || a[index].ny != b[index].ny
            || a[index].nz != b[index].nz) return false;
    }
    return true;
}

bool sameDiagnostics(const QVector<pointcloud::SeamFusionDiagnostic> &a,
                     const QVector<pointcloud::SeamFusionDiagnostic> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype index = 0; index < a.size(); ++index) {
        const pointcloud::SeamFusionDiagnostic &x = a[index];
        const pointcloud::SeamFusionDiagnostic &y = b[index];
        if (x.cloudA != y.cloudA || x.cloudB != y.cloudB
            || x.bandPoints != y.bandPoints || x.bandPointsA != y.bandPointsA
            || x.bandPointsB != y.bandPointsB || x.mutualPairs != y.mutualPairs
            || x.interpolatedPoints != y.interpolatedPoints
            || x.unmatchedDiscarded != y.unmatchedDiscarded
            || x.unmatchedPreserved != y.unmatchedPreserved
            || x.decisionCells != y.decisionCells || x.corePoints != y.corePoints
            || x.applied != y.applied || x.actualOverlapValid != y.actualOverlapValid
            || x.projectedAMin != y.projectedAMin || x.projectedAMax != y.projectedAMax
            || x.projectedBMin != y.projectedBMin || x.projectedBMax != y.projectedBMax
            || x.actualOverlapMin != y.actualOverlapMin
            || x.actualOverlapMax != y.actualOverlapMax
            || x.seamProjection != y.seamProjection || x.reason != y.reason) return false;
    }
    return true;
}

bool sameOutcome(const FusionOutcome &a, const FusionOutcome &b, const char *message)
{
    if (!require(a.result.ok == b.result.ok && a.result.cancelled == b.result.cancelled
                 && a.result.outputPoints == b.result.outputPoints, message)) return false;
    if (!require(samePoints(a.points, b.points),
                 "parallel fusion must keep every point and its order")) return false;
    if (!require(a.cloudIds == b.cloudIds && a.sourceIndices == b.sourceIndices
                 && a.scanRatios == b.scanRatios,
                 "parallel fusion must keep the source mapping")) return false;
    if (!require(sameDiagnostics(a.result.diagnostics, b.result.diagnostics),
                 "parallel fusion must keep the seam diagnostics")) return false;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QVector<pointcloud::RobotBaseFrame> malformed = {malformedFrame(), malformedFrame()};
    const auto registration = pointcloud::registerRobotBaseFrames(std::move(malformed));
    if (!require(!registration.ok && registration.error.contains(QStringLiteral("长度不一致")),
                 "registration must reject malformed source mapping")) return 1;

    pointcloud::MultiFrameRegistrationResult merge;
    merge.points = {{1.0f, 2.0f, 3.0f}};
    merge.cloudIds = {0};
    merge.scanRatios = {0.0f};
    merge.frameMetadata.resize(2);
    merge.registrationCorrections.resize(2);
    const auto originalPoints = merge.points;

    pointcloud::SeamFusionOptions enabled;
    enabled.enabled = true;
    const auto rejected = pointcloud::applyTrajectorySeamFusion(&merge, enabled);
    if (!require(!rejected.ok && !rejected.cancelled
                 && rejected.error.contains(QStringLiteral("长度不一致")),
                 "seam must reject malformed source mapping")) return 1;
    if (!require(merge.points.size() == originalPoints.size()
                 && merge.points[0].x == originalPoints[0].x
                 && merge.points[0].y == originalPoints[0].y
                 && merge.points[0].z == originalPoints[0].z,
                 "failed seam modified input points")) return 1;

    pointcloud::SeamFusionOptions disabled;
    disabled.enabled = false;
    const auto unchanged = pointcloud::applyTrajectorySeamFusion(&merge, disabled);
    if (!require(unchanged.ok && unchanged.outputPoints == originalPoints.size(),
                 "disabled seam must preserve the point cloud")) return 1;

    pointcloud::MultiFrameRegistrationResult valid;
    valid.frameMetadata.resize(2);
    valid.registrationCorrections.resize(2);
    valid.frameMetadata[0].startBaseFromFlange.setToIdentity();
    valid.frameMetadata[0].endBaseFromFlange.setToIdentity();
    valid.frameMetadata[0].endBaseFromFlange(1, 3) = 10.0f;
    valid.frameMetadata[1].startBaseFromFlange.setToIdentity();
    valid.frameMetadata[1].startBaseFromFlange(0, 3) = 5.0f;
    valid.frameMetadata[1].endBaseFromFlange.setToIdentity();
    valid.frameMetadata[1].endBaseFromFlange(0, 3) = 5.0f;
    valid.frameMetadata[1].endBaseFromFlange(1, 3) = 10.0f;
    for (int i = 5; i <= 10; ++i) {
        valid.points.push_back({float(i), 2.0f, 0.0f});
        valid.cloudIds.push_back(0);
        valid.sourceIndices.push_back(i);
        valid.scanRatios.push_back(0.5f);
        valid.points.push_back({float(i), 2.0f, 0.0f});
        valid.cloudIds.push_back(1);
        valid.sourceIndices.push_back(i);
        valid.scanRatios.push_back(0.5f);
    }
    // This point lies in the projected seam band but has no mutual neighbour;
    // it must survive the fusion pass as an unmatched source point.
    valid.points.push_back({7.0f, 2.0f, 100.0f});
    valid.cloudIds.push_back(0);
    valid.sourceIndices.push_back(700);
    valid.scanRatios.push_back(0.5f);
    pointcloud::MultiFrameRegistrationResult parallelInput = valid;
    pointcloud::SeamFusionOptions sequentialOptions = enabled;
    sequentialOptions.parallelEnabled = false;
    const auto fused = pointcloud::applyTrajectorySeamFusion(&valid, sequentialOptions);
    if (!require(fused.ok && fused.outputPoints > 0 && !fused.diagnostics.isEmpty()
                 && fused.diagnostics.first().actualOverlapValid
                 && fused.diagnostics.first().applied,
                 "valid overlapping frames must produce seam diagnostics and fused points")) return 1;
    if (!require(fused.diagnostics.first().unmatchedPreserved > 0,
                 "unmatched seam points must be preserved")) return 1;

    pointcloud::SeamFusionOptions parallelOptions = enabled;
    parallelOptions.parallelEnabled = true;
    parallelOptions.maxParallelTasks = 2;
    const auto parallelFused = pointcloud::applyTrajectorySeamFusion(&parallelInput, parallelOptions);
    if (!require(parallelFused.ok && parallelFused.outputPoints == fused.outputPoints
                 && parallelInput.points.size() == valid.points.size()
                 && parallelInput.cloudIds == valid.cloudIds
                 && parallelInput.sourceIndices == valid.sourceIndices
                 && parallelInput.scanRatios == valid.scanRatios,
                 "parallel seam fusion must preserve sequential output mappings")) return 1;
    for (qsizetype index = 0; index < valid.points.size(); ++index) {
        if (!require(parallelInput.points[index].x == valid.points[index].x
                     && parallelInput.points[index].y == valid.points[index].y
                     && parallelInput.points[index].z == valid.points[index].z,
                     "parallel seam fusion must preserve sequential point order")) return 1;
    }
    if (!require(parallelFused.diagnostics.size() == fused.diagnostics.size()
                 && parallelFused.diagnostics.first().mutualPairs == fused.diagnostics.first().mutualPairs
                 && parallelFused.diagnostics.first().unmatchedPreserved
                     == fused.diagnostics.first().unmatchedPreserved,
                 "parallel seam fusion must preserve diagnostics")) return 1;

    pointcloud::SeamFusionOptions cancelledOptions = enabled;
    cancelledOptions.isCancelled = [] { return true; };
    const auto cancelled = pointcloud::applyTrajectorySeamFusion(&valid, cancelledOptions);
    if (!require(cancelled.cancelled && !cancelled.ok
                 && valid.points.size() == fused.outputPoints,
                 "cancelled seam must not publish a new result")) return 1;

    pointcloud::MultiFrameRegistrationResult midCancelledInput = valid;
    // std::atomic: with parallelEnabled the cancellation callback is invoked
    // from several worker threads, so a plain int would race in the test.
    auto cancellationChecks = std::make_shared<std::atomic<int>>(0);
    pointcloud::SeamFusionOptions midCancelledOptions = enabled;
    midCancelledOptions.isCancelled = [cancellationChecks] {
        return cancellationChecks->fetch_add(1) + 1 >= 2;
    };
    const qsizetype beforeMidCancellation = midCancelledInput.points.size();
    const auto midCancelled = pointcloud::applyTrajectorySeamFusion(
        &midCancelledInput, midCancelledOptions);
    if (!require(midCancelled.cancelled && !midCancelled.ok
                 && midCancelledInput.points.size() == beforeMidCancellation,
                 "mid-seam cancellation must preserve the input cloud")) return 1;

    pointcloud::SeamFusionOptions tokenCancelledOptions = enabled;
    auto token = std::make_shared<std::atomic_bool>(true);
    tokenCancelledOptions.cancellation = token;
    const auto tokenCancelled = pointcloud::applyTrajectorySeamFusion(&valid, tokenCancelledOptions);
    if (!require(tokenCancelled.cancelled && !tokenCancelled.ok,
                 "raised token must cancel seam fusion")) return 1;

    pointcloud::MultiFrameRegistrationResult missingMetadata = valid;
    missingMetadata.registrationCorrections.clear();
    const auto metadataFailure = pointcloud::applyTrajectorySeamFusion(
        &missingMetadata, enabled);
    if (!require(!metadataFailure.ok && metadataFailure.error.contains(QStringLiteral("元数据")),
                 "missing registration metadata must fail closed")) return 1;

    // ---------------------------------------------------------------------
    // Parallel seam fusion over Qt-implicitly-shared inputs (refcount > 1).
    //
    // Sequential output is the reference: point count, point order, source
    // mapping and every diagnostic field must survive parallelEnabled with
    // maxParallelTasks 1/2/4, repeated calls and inclined input.
    // ---------------------------------------------------------------------
    constexpr int chainFrames = 9;
    constexpr int chainX = 40;
    constexpr int chainY = 200;

    {
        const ChainFixture source = makeChain(chainFrames, chainX, chainY, 6.0f, 0.0f, 5.0f);
        // Extra shared copies of every input container stay alive across the
        // parallel run: this is the Qt refcount > 1 state in which a non-const
        // worker read used to detach a buffer its peers were still reading.
        QVector<pointcloud::Point3D> retainedPoints = source.points;
        const QVector<int> retainedCloudIds = source.cloudIds;
        const QVector<qsizetype> retainedSourceIndices = source.sourceIndices;
        const QVector<float> retainedScanRatios = source.scanRatios;
        QVector<pointcloud::Point3D> originalPoints;
        originalPoints.reserve(source.points.size());
        for (const pointcloud::Point3D &point : source.points) originalPoints.push_back(point);

        const FusionOutcome sequential = runFusion(source, false, 0, 1.0f);
        if (!require(sequential.result.ok && !sequential.result.parallelEnabled
                     && sequential.result.outputPoints > 0,
                     "sequential chain fusion must succeed on a single task")) return 1;
        if (!require(sequential.result.diagnostics.size() == chainFrames - 1,
                     "a linear chain must report exactly one diagnostic per seam")) return 1;
        if (!require(sequential.result.outputPoints < source.points.size(),
                     "an overlapping chain must fuse band points away")) return 1;
        qsizetype appliedSeams = 0;
        for (const pointcloud::SeamFusionDiagnostic &diagnostic : sequential.result.diagnostics)
            if (diagnostic.applied) ++appliedSeams;
        if (!require(appliedSeams == chainFrames - 1,
                     "every overlapping seam must apply nearest-neighbour fusion")) return 1;

        for (const int tasks : {1, 2, 4}) {
            const FusionOutcome parallel = runFusion(source, true, tasks, 1.0f);
            if (!require(parallel.result.parallelEnabled == (tasks > 1),
                         "requested parallel task count must be honoured")) return 1;
            if (!sameOutcome(sequential, parallel,
                             "parallel seam fusion must match the sequential run")) return 1;
        }

        const FusionOutcome repeatedA = runFusion(source, true, 4, 1.0f);
        const FusionOutcome repeatedB = runFusion(source, true, 2, 1.0f);
        if (!sameOutcome(repeatedA, repeatedB,
                         "repeated parallel seam fusion must stay deterministic")) return 1;

        // The retained shared copies must still hold the untouched input: a
        // detaching worker would have rewritten or resized these buffers.
        if (!require(retainedPoints.size() == originalPoints.size()
                     && retainedCloudIds.size() == originalPoints.size()
                     && retainedSourceIndices.size() == originalPoints.size()
                     && retainedScanRatios.size() == originalPoints.size(),
                     "parallel seam fusion must not resize the shared input buffers")) return 1;
        if (!require(samePoints(retainedPoints, originalPoints),
                     "parallel seam fusion must not mutate the shared input points")) return 1;
    }

    {
        // Cancellation raised while the parallel waves are running.
        const ChainFixture source = makeChain(chainFrames, chainX, chainY, 6.0f, 0.0f, 5.0f);
        const qsizetype beforeCancellation = source.points.size();
        auto checks = std::make_shared<std::atomic<int>>(0);
        const FusionOutcome cancelled = runFusion(source, true, 4, 1.0f, [checks] {
            return checks->fetch_add(1) + 1 >= 4;
        });
        if (!require(cancelled.result.cancelled && !cancelled.result.ok
                     && cancelled.points.size() == beforeCancellation,
                     "cancellation during parallel fusion must not publish a result")) return 1;
    }

    {
        // Frames that do not overlap at all: every seam must be rejected with
        // a structured diagnostic and the cloud must pass through untouched.
        const ChainFixture spaced = makeChain(4, chainX, chainY, 12.0f, 0.0f, 5.0f);
        const qsizetype beforeSpaced = spaced.points.size();
        const FusionOutcome outcome = runFusion(spaced, true, 4, 1.0f);
        if (!require(outcome.result.ok && !outcome.result.diagnostics.isEmpty()
                     && outcome.result.outputPoints == beforeSpaced,
                     "a non-overlapping chain must pass through as a structured success")) return 1;
        for (const pointcloud::SeamFusionDiagnostic &diagnostic : outcome.result.diagnostics) {
            if (!require(!diagnostic.actualOverlapValid && !diagnostic.applied
                         && diagnostic.reason.contains(
                                QStringLiteral("seam_outside_actual_overlap")),
                         "a non-overlapping seam must be rejected with the overlap reason")) return 1;
        }
    }

    {
        // Empty fusion band: valid overlap geometry, but no point falls inside
        // the requested half width, so nothing may be fused away.
        const ChainFixture narrow = makeChain(4, chainX, chainY, 6.0f, 0.0f, 5.0f);
        const qsizetype beforeNarrow = narrow.points.size();
        const FusionOutcome outcome = runFusion(narrow, true, 4, 0.001f);
        if (!require(outcome.result.ok && outcome.result.diagnostics.size() == 3
                     && outcome.result.outputPoints == beforeNarrow,
                     "an empty fusion band must preserve the whole input cloud")) return 1;
        for (const pointcloud::SeamFusionDiagnostic &diagnostic : outcome.result.diagnostics) {
            if (!require(diagnostic.bandPoints == 0 && !diagnostic.applied,
                         "an empty fusion band must report zero band points")) return 1;
        }
    }

    {
        // 30 degree inclined chain: the seam pipeline may succeed or reject the
        // geometry, but it must return a structured result and never abort.
        const ChainFixture inclined = makeChain(5, chainX, chainY, 6.0f, 30.0f, 5.0f);
        const FusionOutcome sequential = runFusion(inclined, false, 0, 1.0f);
        const FusionOutcome parallel = runFusion(inclined, true, 4, 1.0f);
        const auto structured = [](const pointcloud::SeamFusionResult &value) {
            if (value.cancelled) return false;
            return value.ok ? (value.outputPoints > 0 && !value.diagnostics.isEmpty())
                            : !value.error.isEmpty();
        };
        if (!require(structured(sequential.result),
                     "a 30 degree chain must succeed or fail in a structured way")) return 1;
        if (!require(structured(parallel.result),
                     "a parallel 30 degree chain must succeed or fail in a structured way")) return 1;
        if (!sameOutcome(sequential, parallel,
                         "a 30 degree chain must match between serial and parallel modes")) return 1;
    }

    std::cout << "Seam fusion safety tests passed.\n";
    return 0;
}
