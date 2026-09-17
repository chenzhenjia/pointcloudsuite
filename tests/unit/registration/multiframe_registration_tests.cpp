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
