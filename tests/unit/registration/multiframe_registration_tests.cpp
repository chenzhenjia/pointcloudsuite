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

// Same construction as dropoutPair, but the source-only island is far larger
// than the shared one, so the bidirectional hit ratio drops below 0.25 while
// the accuracy among the matched points stays excellent. It reproduces the
// 5_40 situation: a locally accurate correction on a pair that barely sees
// common surface.
DropoutPair lowVisibilityDropoutPair()
{
    constexpr float basePitch = 1.0f;
    constexpr float islandPitch = 0.5f;
    constexpr float baseMaxX = 70.0f;
    constexpr float baseMaxY = 46.0f;
    constexpr float sharedMinX = 4.0f;
    constexpr float sharedMaxX = 20.0f;
    constexpr float islandMinY = 13.0f;
    constexpr float islandMaxY = 29.0f;
    constexpr float extraMinX = 40.0f;
    constexpr float extraMaxX = 64.0f;
    constexpr float holeMinX = 20.0f;
    constexpr float holeMaxX = 70.0f;
    constexpr float holeMinY = 1.0f;
    constexpr float holeMaxY = 45.0f;

    DropoutPair pair;
    pair.reference.sourceFile = QStringLiteral("low_visibility_reference.ply");
    pair.moving.sourceFile = QStringLiteral("low_visibility_moving.ply");
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
            appendDropoutPoint(&pair.moving, x, y, 12.0f);
            appendDropoutPoint(&pair.reference, x, y, 12.0f);
        }
    }
    // Source-only island: at least 20 mm away from every reference point, so
    // none of its points can find a counterpart inside the 15 mm radius.
    for (float x = extraMinX; x < extraMaxX; x += islandPitch)
        for (float y = holeMinY; y < holeMaxY; y += islandPitch)
            appendDropoutPoint(&pair.moving, x, y, 12.0f);

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


// ---------------------------------------------------------------------
// WP6: 2.5D height-map registration (controlled iteration).
//
// Synthetic surface with a raised plateau, a hole and the plateau cliffs.
// Two frames of the same surface carry a known residual, so the height-map
// search has a recoverable dx/dy (and a small dz/yaw) to find.
// ---------------------------------------------------------------------
struct HeightMapSurface {
    float pitch = 0.8f;
    float maximumX = 60.0f;
    float maximumY = 45.0f;
    float stepMinimumX = 20.0f;
    float stepMaximumX = 36.0f;
    float stepMinimumY = 12.0f;
    float stepMaximumY = 34.0f;
    float stepHeight = 6.0f;
    float tiltPerMm = 0.02f;
    bool cliffs = true;
    bool hole = true;
    // Real micro-roughness is not repeated across scans; a fixture that must be
    // structurally featureless therefore carries no position-locked jitter.
    bool jitter = true;
};

struct HeightMapPair {
    QVector<pointcloud::Point3D> surface;
    pointcloud::RobotBaseFrame reference;
    pointcloud::RobotBaseFrame moving;
};

// Deterministic sub-millimetre roughness: a correct alignment still leaves a
// small non-zero residual, so the score never collapses to a single value.
QVector3D heightMapJitter(float x, float y)
{
    const qint64 key = qint64(std::lround(double(x) * 1000.0)) * 73856093LL
        + qint64(std::lround(double(y) * 1000.0)) * 19349663LL;
    const auto channel = [key](int index) {
        quint32 state = quint32(key + qint64(index) * 2654435761LL) + 1013904223u;
        state ^= state >> 13;
        state *= 2246822519u;
        state ^= state >> 17;
        return float(double(state % 2001u) / 1000.0) - 1.0f;
    };
    return {channel(0) * 0.05f, channel(1) * 0.05f, channel(2) * 0.05f};
}

QVector<pointcloud::Point3D> heightMapSurfacePoints(const HeightMapSurface &spec)
{
    QVector<pointcloud::Point3D> points;
    const auto elevation = [&spec](float x, float y) {
        const bool onStep = x >= spec.stepMinimumX && x <= spec.stepMaximumX
            && y >= spec.stepMinimumY && y <= spec.stepMaximumY;
        return spec.tiltPerMm * x + (onStep ? spec.stepHeight : 0.0f);
    };
    for (float x = 0.0f; x <= spec.maximumX; x += spec.pitch) {
        for (float y = 0.0f; y <= spec.maximumY; y += spec.pitch) {
            if (spec.hole && x >= 42.0f && x <= 50.0f && y >= 6.0f && y <= 18.0f) continue;
            const QVector3D jitter = spec.jitter ? heightMapJitter(x, y) : QVector3D();
            points.push_back(pointcloud::Point3D{x + jitter.x(), y + jitter.y(),
                                                 elevation(x, y) + jitter.z()});
        }
    }
    if (spec.cliffs) {
        for (float z = spec.pitch; z < spec.stepHeight; z += spec.pitch) {
            for (float y = spec.stepMinimumY; y <= spec.stepMaximumY; y += spec.pitch) {
                for (float x : {spec.stepMinimumX, spec.stepMaximumX}) {
                    const QVector3D jitter = spec.jitter ? heightMapJitter(x, y) : QVector3D();
                    points.push_back(pointcloud::Point3D{x + jitter.x(), y + jitter.y(),
                        spec.tiltPerMm * x + z + jitter.z()});
                }
            }
            for (float x = spec.stepMinimumX; x <= spec.stepMaximumX; x += spec.pitch) {
                for (float y : {spec.stepMinimumY, spec.stepMaximumY}) {
                    const QVector3D jitter = spec.jitter ? heightMapJitter(x, y) : QVector3D();
                    points.push_back(pointcloud::Point3D{x + jitter.x(), y + jitter.y(),
                        spec.tiltPerMm * x + z + jitter.z()});
                }
            }
        }
    }
    // The scanner order follows the travel axis, so the points are sorted by X
    // before the scanRatios are derived from their position.
    std::sort(points.begin(), points.end(),
              [](const pointcloud::Point3D &left, const pointcloud::Point3D &right) {
                  return left.x < right.x;
              });
    return points;
}

pointcloud::RobotBaseFrame heightMapFrame(const QString &sourceFile,
                                          const QVector<pointcloud::Point3D> &surface,
                                          float offsetX, float offsetY, float offsetZ,
                                          float yawDegrees, float travelStart,
                                          float travelLength, float maximumX)
{
    pointcloud::RobotBaseFrame frame;
    frame.sourceFile = sourceFile;
    frame.fullPoints.reserve(surface.size());
    frame.samplePoints.reserve(surface.size());
    frame.sourceIndices.reserve(surface.size());
    frame.scanRatios.reserve(surface.size());
    QMatrix4x4 yaw;
    yaw.setToIdentity();
    yaw.rotate(yawDegrees, 0.0f, 0.0f, 1.0f);
    for (const pointcloud::Point3D &point : surface) {
        const QVector3D value = yaw.map(QVector3D(point.x, point.y, point.z))
            + QVector3D(offsetX, offsetY, offsetZ);
        const pointcloud::Point3D transformed{value.x(), value.y(), value.z(),
                                              point.nx, point.ny, point.nz};
        frame.fullPoints.push_back(transformed);
        frame.samplePoints.push_back(transformed);
        frame.sourceIndices.push_back(frame.sourceIndices.size());
        frame.scanRatios.push_back(qBound(0.0f, point.x / maximumX, 1.0f));
    }
    frame.declaredCount = frame.fullPoints.size();
    frame.startBaseFromFlange.setToIdentity();
    frame.startBaseFromFlange.setColumn(3, QVector4D(travelStart, 0.0f, 0.0f, 1.0f));
    frame.endBaseFromFlange.setToIdentity();
    frame.endBaseFromFlange.setColumn(3, QVector4D(travelStart + travelLength, 0.0f, 0.0f, 1.0f));
    frame.dominantTravelAxis = 0;
    frame.signedTravel = travelLength;
    return frame;
}

HeightMapPair heightMapPair(const HeightMapSurface &spec, float offsetX, float offsetY,
                            float offsetZ = 0.0f, float yawDegrees = 0.0f)
{
    HeightMapPair pair;
    pair.surface = heightMapSurfacePoints(spec);
    pair.reference = heightMapFrame(QStringLiteral("heightmap_reference.ply"), pair.surface,
                                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, spec.maximumX);
    pair.moving = heightMapFrame(QStringLiteral("heightmap_moving.ply"), pair.surface,
                                 offsetX, offsetY, offsetZ, yawDegrees, 10.0f, 60.0f,
                                 spec.maximumX);
    return pair;
}

pointcloud::IcpOptions heightMapOptions()
{
    pointcloud::IcpOptions options;
    options.planePrealignmentEnabled = true;
    options.planeIdentityTrackingEnabled = false;
    options.tangentCoarseAlignmentEnabled = false;
    options.tangentCoarseAlignmentAuto = false;
    options.wideStructureDiagnosticEnabled = false;
    options.parallelEnabled = false;
    options.heightMapRegistrationEnabled = true;
    return options;
}

// Human-readable snapshot of the 2.5D decision, used as the failure text so a
// rejected fixture always reports why it was rejected.
std::string heightMapReason(const char *label, const pointcloud::IcpDiagnostics &diagnostic)
{
    return (QStringLiteral("%1: reject=%2 before=%3 after=%4 rel=%5 dx=%6 dy=%7 dz=%8 yaw=%9 "
                           "grid=%10 wh=%11x%12 srcRatio=%13 tgtRatio=%14 srcPts=%15 tgtPts=%16 "
                           "levels=%17 degen=%18 traj=%19 source=%20 accepted=%21 legacy=%22 "
                           "legacyReason=%23 corrT=%24 corrA=%25 visHard=%26")
        .arg(QString::fromUtf8(label), diagnostic.heightMapRejectReason)
        .arg(diagnostic.heightMapScoreBefore).arg(diagnostic.heightMapScoreAfter)
        .arg(diagnostic.heightMapRelativeImprovement)
        .arg(diagnostic.heightMapTranslationX).arg(diagnostic.heightMapTranslationY)
        .arg(diagnostic.heightMapTranslationZ).arg(diagnostic.heightMapYawDegrees)
        .arg(diagnostic.heightMapGridResolutionMm)
        .arg(diagnostic.heightMapWidth).arg(diagnostic.heightMapHeight)
        .arg(diagnostic.heightMapSourceValidRatio).arg(diagnostic.heightMapTargetValidRatio)
        .arg(diagnostic.heightMapSourceValidPoints).arg(diagnostic.heightMapTargetValidPoints)
        .arg(diagnostic.heightMapRestrictedIcpCompletedLevels)
        .arg(diagnostic.heightMapRestrictedIcpDegenerate ? 1 : 0)
        .arg(diagnostic.trajectoryDiagnosticReason)
        .arg(diagnostic.selectedRegistrationSource)
        .arg(diagnostic.accepted ? 1 : 0)
        .arg(diagnostic.legacyIcpAccepted ? 1 : 0)
        .arg(diagnostic.legacyIcpRejectReason)
        .arg(diagnostic.correctionTranslation).arg(diagnostic.correctionAngleDegrees)
        .arg(diagnostic.visibilityHardRejected ? 1 : 0)).toStdString();
}

bool heightMapDiagnosticsComplete(const pointcloud::IcpDiagnostics &diagnostic)
{
    return diagnostic.heightMapGridResolutionMm >= 0.5f
        && diagnostic.heightMapGridResolutionMm <= 2.0f
        && diagnostic.heightMapWidth > 0 && diagnostic.heightMapHeight > 0
        && diagnostic.heightMapScoreBefore > 0.0f
        && std::isfinite(diagnostic.heightMapSourceValidRatio)
        && std::isfinite(diagnostic.heightMapTargetValidRatio)
        && diagnostic.selectedRegistrationSource == QStringLiteral("heightmap_restricted_icp");
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




    // (1)(4)(11) Known dx/dy recovery on the stepped surface; the legacy ICP
    // passes as well, so the height-map result must be the formal source.
    {
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOptions());
        if (!require(result.icpDiagnostics.size() == 2,
                     "the height-map fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(d.heightMapAttempted && d.heightMapAccepted,
                     "the stepped surface must pass the 2.5D acceptance")) return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("heightmap_restricted_icp"),
                     "both paths passing must select heightmap_restricted_icp")) return 1;
        if (!require(!d.heightMapFallbackToIcp,
                     "a height-map pass must not be reported as a fallback")) return 1;
        if (!require(std::abs(d.heightMapTranslationX + 3.0f) <= 1.0f
                         && std::abs(d.heightMapTranslationY - 2.0f) <= 1.0f,
                     "the height-map search must recover the known dx/dy")) return 1;
        if (!require(d.heightMapRelativeImprovement >= 0.01f
                         && d.heightMapScoreAfter < d.heightMapScoreBefore,
                     "the candidate must improve the robot initial score by 1%")) return 1;
        if (!require(d.heightMapRestrictedIcpCompletedLevels == 3
                         && !d.heightMapRestrictedIcpDegenerate,
                     "the restricted ICP must complete three non-degenerate levels")) return 1;
        if (!require(d.heightMapSourceValidRatio >= 0.25f && d.heightMapTargetValidRatio >= 0.25f
                         && d.heightMapSourceValidPoints >= 100 && d.heightMapTargetValidPoints >= 100,
                     "the height map must report both valid ratios and point counts")) return 1;
        if (!require(d.correctionTranslation <= 10.0f && d.correctionAngleDegrees <= 1.0f,
                     "the final correction must stay inside the strict budget")) return 1;
        if (!require(d.legacyIcpComparisonAvailable && d.legacyIcpAccepted,
                     "the legacy baseline must be recorded for every pair")) return 1;
        if (!require(!d.visibilityHardRejected,
                     "a well observed pair must not be visibility-rejected")) return 1;
        if (!require(heightMapDiagnosticsComplete(d),
                     "the 2.5D diagnostics must be fully populated")) return 1;
        if (!require(result.registrationCorrections.size() == 2
                         && std::abs(result.registrationCorrections[1](0, 3) + 3.0f) <= 1.0f,
                     "the committed correction must carry the height-map result")) return 1;
    }

    // (2) Small dz and yaw: only dx/dy/dz/yaw are open, so the restricted
    // degrees of freedom must still recover a small dz and a small yaw.
    {
        const HeightMapPair dzPair = heightMapPair(HeightMapSurface{}, 2.5f, -1.5f, 2.0f, 0.0f);
        const auto dzResult = pointcloud::registerRobotBaseFrames(
            {dzPair.reference, dzPair.moving}, heightMapOptions());
        if (!require(dzResult.icpDiagnostics.size() == 2,
                     "the dz fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &dzDiagnostic = dzResult.icpDiagnostics[1];
        if (!require(std::abs(dzDiagnostic.heightMapTranslationZ + 2.0f) <= 1.0f,
                     "the second stage must recover a small dz")) return 1;
        if (!require(dzDiagnostic.heightMapAttempted,
                     "the dz fixture must run the height-map attempt")) return 1;

        const HeightMapPair yawPair = heightMapPair(HeightMapSurface{}, 0.0f, 0.0f, 0.0f, 0.4f);
        const auto yawResult = pointcloud::registerRobotBaseFrames(
            {yawPair.reference, yawPair.moving}, heightMapOptions());
        if (!require(yawResult.icpDiagnostics.size() == 2,
                     "the yaw fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &yawDiagnostic = yawResult.icpDiagnostics[1];
        if (!require(yawDiagnostic.heightMapAttempted && yawDiagnostic.heightMapAccepted,
                     heightMapReason("the yaw fixture must pass the 2.5D acceptance",
                                     yawDiagnostic).c_str())) return 1;
        // The restricted degrees of freedom must still recover the small yaw:
        // the height map only initialises it, the restricted point-to-plane ICP
        // finishes it, and both stay inside the four-dimensional subspace.
        if (!require(std::abs(yawDiagnostic.correctionAngleDegrees - 0.4f) <= 0.2f,
                     "the restricted ICP must recover the small yaw")) return 1;
        const QVector3D rotatedScanAxis = yawResult.registrationCorrections[1].mapVector(
            QVector3D(0.0f, 0.0f, 1.0f));
        if (!require(rotatedScanAxis.z() >= 0.9999f
                         && std::abs(rotatedScanAxis.x()) <= 1.0e-4f
                         && std::abs(rotatedScanAxis.y()) <= 1.0e-4f,
                     "the restricted correction must rotate about the scan axis only")) return 1;
        if (!require(yawDiagnostic.correctionAngleDegrees <= 1.0f
                         && yawDiagnostic.correctionTranslation <= 10.0f,
                     "the yaw correction must stay inside the strict budget")) return 1;
    }

    // (3) A large flat plane must never be forced into an alignment.
    {
        HeightMapSurface flat;
        flat.stepHeight = 0.0f;
        flat.cliffs = false;
        flat.hole = false;
        flat.tiltPerMm = 0.0f;
        flat.jitter = false;
        const HeightMapPair pair = heightMapPair(flat, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOptions());
        if (!require(result.icpDiagnostics.size() == 2,
                     "the flat fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(d.heightMapRejectReason == QStringLiteral("heightmap_candidate_ambiguous"),
                     heightMapReason("a featureless plane must be rejected as ambiguous", d).c_str()))
            return 1;
        if (!require(!d.heightMapAccepted && !d.accepted,
                     "an ambiguous candidate must never be accepted")) return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("none"),
                     "a rejected height map with a failed legacy path selects no source")) return 1;
    }

    // (5) A height map thinner than the minimum valid ratio is rejected.
    {
        HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        QVector<pointcloud::Point3D> sparsePoints;
        QVector<qsizetype> sparseIndices;
        QVector<float> sparseRatios;
        for (qsizetype index = 0; index < pair.moving.fullPoints.size(); ++index) {
            if (index % 7 != 0) continue;
            sparsePoints.push_back(pair.moving.fullPoints.at(index));
            sparseIndices.push_back(pair.moving.sourceIndices.at(index));
            sparseRatios.push_back(pair.moving.scanRatios.at(index));
        }
        pair.moving.fullPoints = sparsePoints;
        pair.moving.samplePoints = sparsePoints;
        pair.moving.sourceIndices = sparseIndices;
        pair.moving.scanRatios = sparseRatios;
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOptions());
        if (!require(result.icpDiagnostics.size() == 2,
                     "the sparse fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(d.heightMapAttempted && !d.heightMapAccepted,
                     "a height map below the valid ratio must not be accepted")) return 1;
        if (!require(d.heightMapRejectReason == QStringLiteral("heightmap_insufficient_data"),
                     "a height map below the valid ratio must report insufficient data")) return 1;
        if (!require(d.heightMapSourceValidRatio < 0.25f || d.heightMapSourceValidPoints < 100,
                     "the rejected pair must show the thin height map")) return 1;
    }

    // (6) Fewer than 100 valid points on either side is rejected.
    {
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 54.0f, 39.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOptions());
        if (!require(result.icpDiagnostics.size() == 2,
                     "the tiny-overlap fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(d.heightMapAttempted && !d.heightMapAccepted,
                     "a tiny overlap must not be accepted")) return 1;
        if (!require(d.heightMapRejectReason == QStringLiteral("heightmap_insufficient_data")
                         || d.heightMapRejectReason == QStringLiteral("heightmap_region_invalid"),
                     "a tiny overlap must be rejected as insufficient data")) return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("none")
                         || d.selectedRegistrationSource == QStringLiteral("legacy_icp"),
                     "a tiny overlap must report a defined source")) return 1;
    }

    // (7) The grid resolution is estimated automatically inside 0.5..2.0 mm.
    {
        HeightMapSurface fine;
        fine.pitch = 0.4f;
        const auto fineResult = pointcloud::registerRobotBaseFrames(
            {heightMapPair(fine, 3.0f, -2.0f).reference, heightMapPair(fine, 3.0f, -2.0f).moving},
            heightMapOptions());
        const pointcloud::IcpDiagnostics &fineDiagnostic = fineResult.icpDiagnostics[1];
        if (!require(fineDiagnostic.heightMapGridResolutionMm == 0.5f,
                     "a sub-0.5 mm pitch must be clamped to 0.5 mm")) return 1;
        HeightMapSurface coarse;
        coarse.pitch = 2.5f;
        const auto coarseResult = pointcloud::registerRobotBaseFrames(
            {heightMapPair(coarse, 3.0f, -2.0f).reference, heightMapPair(coarse, 3.0f, -2.0f).moving},
            heightMapOptions());
        const pointcloud::IcpDiagnostics &coarseDiagnostic = coarseResult.icpDiagnostics[1];
        if (!require(coarseDiagnostic.heightMapGridResolutionMm == 2.0f,
                     "a coarser pitch must be clamped to 2.0 mm")) return 1;
        if (!require(fineDiagnostic.heightMapWidth > 0 && fineDiagnostic.heightMapHeight > 0
                         && coarseDiagnostic.heightMapWidth > 0
                         && coarseDiagnostic.heightMapHeight > 0,
                     "both grid estimates must produce a height map")) return 1;
    }

    // (8) The strict 10 mm / 1 deg budget is never crossed.
    {
        pointcloud::IcpOptions zeroBudget = heightMapOptions();
        zeroBudget.heightMapMaxTranslationMm = 0.0f;
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto rejected = pointcloud::registerRobotBaseFrames({pair.reference, pair.moving}, zeroBudget);
        const pointcloud::IcpDiagnostics &rejectedDiagnostic = rejected.icpDiagnostics[1];
        if (!require(rejectedDiagnostic.heightMapRejectReason
                         == QStringLiteral("heightmap_candidate_exceeds_budget"),
                     "an empty translation budget must reject the candidate")) return 1;
        if (!require(!rejectedDiagnostic.heightMapAccepted,
                     "an over-budget candidate must not be accepted")) return 1;

        const HeightMapPair farPair = heightMapPair(HeightMapSurface{}, 25.0f, -18.0f);
        const auto far = pointcloud::registerRobotBaseFrames(
            {farPair.reference, farPair.moving}, heightMapOptions());
        const pointcloud::IcpDiagnostics &farDiagnostic = far.icpDiagnostics[1];
        if (!require(std::abs(farDiagnostic.heightMapTranslationX) <= 10.0f + 1.0e-3f
                         && std::abs(farDiagnostic.heightMapTranslationY) <= 10.0f + 1.0e-3f
                         && std::abs(farDiagnostic.heightMapTranslationZ) <= 10.0f + 1.0e-3f,
                     "a far offset must stay inside the 10 mm strict budget")) return 1;
        if (!require(std::abs(farDiagnostic.heightMapYawDegrees) <= 1.0f + 1.0e-6f,
                     "a candidate must stay inside the 1 degree strict budget")) return 1;
        if (!require(farDiagnostic.correctionTranslation <= 10.0f + 1.0e-4f
                         && farDiagnostic.correctionAngleDegrees <= 1.0f + 1.0e-6f,
                     "the final correction must stay inside the strict budget")) return 1;
    }


    // (9) A restricted ICP that cannot solve its four freedoms must fail in a
    // structured way and must never reach the formal output.
    {
        HeightMapSurface isolated;
        isolated.stepMinimumX = 24.0f;
        isolated.stepMaximumX = 40.0f;
        isolated.stepMinimumY = 16.0f;
        isolated.stepMaximumY = 32.0f;
        isolated.cliffs = false;
        isolated.hole = false;
        isolated.tiltPerMm = 0.0f;
        pointcloud::IcpOptions degenerateOptions = heightMapOptions();
        degenerateOptions.voxelLevels = {3.0f, 1.5f, 0.75f};
        degenerateOptions.correspondenceDistances = {6.0f, 3.0f, 1.5f};
        const HeightMapPair pair = heightMapPair(isolated, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, degenerateOptions);
        if (!require(result.icpDiagnostics.size() == 2,
                     "the degeneracy fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(!(d.accepted && d.heightMapRestrictedIcpDegenerate),
                     "a degenerate restricted ICP must never be accepted")) return 1;
        if (d.heightMapRestrictedIcpDegenerate) {
            if (!require(!d.heightMapAccepted
                             && d.heightMapRejectReason
                                    == QStringLiteral("heightmap_restricted_icp_degenerate"),
                         "a degenerate restricted ICP must report its own failure code")) return 1;
            if (!require(!d.accepted
                             && d.selectedRegistrationSource == QStringLiteral("none"),
                         "a degenerate restricted ICP must not produce a formal source")) return 1;
        }
        if (!require(d.accepted || !d.heightMapRejectReason.isEmpty(),
                     "the degeneracy fixture must fail in a structured way")) return 1;
    }

    // (10) 2.5D fails and the legacy ICP passes: the legacy result is used.
    {
        pointcloud::IcpOptions fallbackOptions = heightMapOptions();
        // An unreachable improvement gate: the height-map path always fails, so
        // the legacy path has to carry the pair.
        fallbackOptions.heightMapMinimumRelativeImprovement = 0.999f;
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, fallbackOptions);
        if (!require(result.icpDiagnostics.size() == 2,
                     "the fallback fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(d.legacyIcpAccepted && d.accepted,
                     heightMapReason("the legacy path must accept the fallback fixture", d).c_str()))
            return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("legacy_icp"),
                     heightMapReason("a height-map failure must fall back to legacy_icp", d).c_str()))
            return 1;
        if (!require(d.heightMapFallbackToIcp && !d.heightMapAccepted,
                     "the fallback must be reported explicitly")) return 1;
        if (!require(d.heightMapRejectReason == QStringLiteral("heightmap_insufficient_improvement"),
                     "the fallback must keep the height-map reject reason")) return 1;
    }

    // (12) 2.5D passes while the legacy acceptance fails: the 2.5D result is
    // still published, because this iteration uses the height-map criteria.
    {
        pointcloud::IcpOptions heightMapOnly = heightMapOptions();
        // An unreachable evidence floor: the legacy structural gate fails in both
        // the strict attempt and the relaxed retry, while the 2.5D acceptance
        // does not use that gate at all.
        heightMapOnly.minimumStructuralMatchedRatio = 5.0f;
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOnly);
        if (!require(result.icpDiagnostics.size() == 2,
                     "the height-map-only fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(!d.legacyIcpAccepted && d.legacyIcpComparisonAvailable,
                     "the legacy baseline must fail without hiding the fixture")) return 1;
        if (!require(d.heightMapAccepted && d.accepted,
                     "the 2.5D result must be published although the legacy gate fails")) return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("heightmap_restricted_icp"),
                     "the 2.5D-only pass must select heightmap_restricted_icp")) return 1;
        if (!require(std::isfinite(d.structuralBidirectionalCoverage3mm),
                     "the legacy structural metric must still be recorded")) return 1;
    }

    // (13) Both paths fail: no formal output is produced for the pair.
    {
        pointcloud::IcpOptions bothFail = heightMapOptions();
        bothFail.heightMapMinimumRelativeImprovement = 0.999f;
        bothFail.minimumStructuralMatchedRatio = 5.0f;
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, bothFail);
        if (!require(result.icpDiagnostics.size() == 2,
                     "the double-failure fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &d = result.icpDiagnostics[1];
        if (!require(!d.accepted && !d.heightMapAccepted,
                     "a double failure must not be accepted")) return 1;
        if (!require(d.selectedRegistrationSource == QStringLiteral("none"),
                     "a double failure must select no registration source")) return 1;
        if (!require(d.failureCode == QStringLiteral("registration_paths_failed"),
                     "a double failure must report its combined failure code")) return 1;
    }

    // (14) Multi-frame pairs are committed strictly in scan order.
    {
        const HeightMapSurface spec;
        const QVector<pointcloud::Point3D> surface = heightMapSurfacePoints(spec);
        const pointcloud::RobotBaseFrame first = heightMapFrame(
            QStringLiteral("hm0.ply"), surface, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, spec.maximumX);
        const pointcloud::RobotBaseFrame second = heightMapFrame(
            QStringLiteral("hm1.ply"), surface, 3.0f, -2.0f, 0.0f, 0.0f, 10.0f, 60.0f, spec.maximumX);
        const pointcloud::RobotBaseFrame third = heightMapFrame(
            QStringLiteral("hm2.ply"), surface, 5.0f, -1.0f, 0.0f, 0.0f, 20.0f, 60.0f, spec.maximumX);
        const QVector<pointcloud::RobotBaseFrame> originals{first, second, third};
        const auto result = pointcloud::registerRobotBaseFrames(
            {first, second, third}, heightMapOptions());
        if (!require(result.registrationCorrections.size() == 3
                         && result.icpDiagnostics.size() == 3,
                     "the three-frame fixture must report three corrections")) return 1;
        if (!require(result.icpDiagnostics[1].targetCloudIndex == 0
                         && result.icpDiagnostics[1].sourceCloudIndex == 1
                         && result.icpDiagnostics[2].targetCloudIndex == 1
                         && result.icpDiagnostics[2].sourceCloudIndex == 2,
                     "the pairs must be processed in adjacent scan order")) return 1;
        if (!require(result.icpDiagnostics[1].accepted && result.icpDiagnostics[2].accepted,
                     "both adjacent pairs must register")) return 1;
        if (!require(result.registrationCorrections[0].isIdentity(),
                     "the reference frame keeps the identity correction")) return 1;
        qsizetype offset = 0;
        for (int cloudId = 0; cloudId < 3; ++cloudId) {
            for (qsizetype index = 0; index < originals[cloudId].fullPoints.size(); ++index) {
                const QVector3D expected = result.registrationCorrections[cloudId].map(
                    QVector3D(originals[cloudId].fullPoints.at(index).x,
                              originals[cloudId].fullPoints.at(index).y,
                              originals[cloudId].fullPoints.at(index).z));
                const pointcloud::Point3D &actual = result.points.at(offset + index);
                if (!require(std::abs(actual.x - expected.x()) <= 1.0e-4f
                                 && std::abs(actual.y - expected.y()) <= 1.0e-4f
                                 && std::abs(actual.z - expected.z()) <= 1.0e-4f,
                             "each frame must carry its own sequential global correction")) return 1;
            }
            offset += originals[cloudId].fullPoints.size();
        }
    }


    // (15) Serial and parallel runs must agree on the points, the source
    // mapping, the matrices and every height-map diagnostic.
    {
        const HeightMapSurface spec;
        const QVector<pointcloud::Point3D> surface = heightMapSurfacePoints(spec);
        const auto makeFrames = [&surface, &spec]() {
            return QVector<pointcloud::RobotBaseFrame>{
                heightMapFrame(QStringLiteral("hm_par0.ply"), surface, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, spec.maximumX),
                heightMapFrame(QStringLiteral("hm_par1.ply"), surface, 3.0f, -2.0f, 0.0f, 0.0f, 10.0f, 60.0f, spec.maximumX),
                heightMapFrame(QStringLiteral("hm_par2.ply"), surface, 5.0f, -1.0f, 0.0f, 0.0f, 20.0f, 60.0f, spec.maximumX)};
        };
        pointcloud::IcpOptions serialOptions = heightMapOptions();
        serialOptions.parallelEnabled = false;
        QVector<pointcloud::RobotBaseFrame> serialInput = makeFrames();
        const QVector<pointcloud::RobotBaseFrame> retained = serialInput;
        const auto serial = pointcloud::registerRobotBaseFrames(std::move(serialInput), serialOptions);
        if (!require(serial.ok && serial.icpDiagnostics.size() == 3,
                     "the parallel fixture must register serially")) return 1;
        for (const int tasks : {1, 2, 4}) {
            pointcloud::IcpOptions options = heightMapOptions();
            options.parallelEnabled = true;
            options.maxParallelTasks = tasks;
            const auto parallel = pointcloud::registerRobotBaseFrames(makeFrames(), options);
            if (!require(parallel.points.size() == serial.points.size()
                             && parallel.cloudIds == serial.cloudIds
                             && parallel.sourceIndices == serial.sourceIndices
                             && parallel.scanRatios == serial.scanRatios,
                         "parallel registration must keep the source mapping")) return 1;
            for (qsizetype index = 0; index < serial.points.size(); ++index) {
                if (!require(parallel.points.at(index).x == serial.points.at(index).x
                                 && parallel.points.at(index).y == serial.points.at(index).y
                                 && parallel.points.at(index).z == serial.points.at(index).z,
                             "parallel registration must keep the point order")) return 1;
            }
            for (qsizetype index = 0; index < serial.registrationCorrections.size(); ++index) {
                for (int element = 0; element < 16; ++element) {
                    if (!require(parallel.registrationCorrections.at(index).constData()[element]
                                     == serial.registrationCorrections.at(index).constData()[element],
                                 "parallel registration must keep the correction matrices")) return 1;
                }
            }
            for (qsizetype index = 0; index < serial.icpDiagnostics.size(); ++index) {
                const pointcloud::IcpDiagnostics &expected = serial.icpDiagnostics.at(index);
                const pointcloud::IcpDiagnostics &actual = parallel.icpDiagnostics.at(index);
                if (!require(actual.accepted == expected.accepted
                                 && actual.selectedRegistrationSource == expected.selectedRegistrationSource
                                 && actual.heightMapAccepted == expected.heightMapAccepted
                                 && actual.heightMapTranslationX == expected.heightMapTranslationX
                                 && actual.heightMapTranslationY == expected.heightMapTranslationY
                                 && actual.heightMapTranslationZ == expected.heightMapTranslationZ
                                 && actual.heightMapYawDegrees == expected.heightMapYawDegrees
                                 && actual.heightMapScoreBefore == expected.heightMapScoreBefore
                                 && actual.heightMapScoreAfter == expected.heightMapScoreAfter
                                 && actual.heightMapGridResolutionMm == expected.heightMapGridResolutionMm
                                 && actual.heightMapRejectReason == expected.heightMapRejectReason,
                             "parallel registration must keep the height-map diagnostics")) return 1;
            }
        }
        if (!require(retained.size() == 3
                         && retained.first().fullPoints.size() == surface.size(),
                     "the height-map path must not mutate the shared input frames")) return 1;
    }

    // (16) Cancellation during the 2.5D work never publishes a partial result.
    {
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        // Cancellation is honoured in the frame preparation, in the height-map
        // attempt (before the restricted ICP) and in every ICP iteration, and a
        // cancelled run must never publish a partial result.
        for (const int limit : {0, 3}) {
            pointcloud::IcpOptions options = heightMapOptions();
            int calls = 0;
            options.isCancelled = [&calls, limit]() { return ++calls > limit; };
            const auto result = pointcloud::registerRobotBaseFrames(
                {pair.reference, pair.moving}, options);
            if (!require(result.cancelled && !result.ok,
                         heightMapReason("a cancelled 2.5D run must not publish a result",
                                         result.icpDiagnostics.isEmpty()
                                             ? pointcloud::IcpDiagnostics() : result.icpDiagnostics.last())
                             .c_str())) return 1;
            if (!require(result.points.isEmpty(),
                         "a cancelled 2.5D run must not publish partial points")) return 1;
            if (!require(result.error == QStringLiteral("已取消"),
                         "a cancelled 2.5D run must report cancellation")) return 1;
        }
    }

    // (17) Repeated calls must not pollute any cache, including after a
    // cancelled run in between.
    {
        const HeightMapSurface spec;
        const QVector<pointcloud::Point3D> surface = heightMapSurfacePoints(spec);
        const auto makeFrames = [&surface, &spec]() {
            return QVector<pointcloud::RobotBaseFrame>{
                heightMapFrame(QStringLiteral("hm_rep0.ply"), surface, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, spec.maximumX),
                heightMapFrame(QStringLiteral("hm_rep1.ply"), surface, 3.0f, -2.0f, 0.0f, 0.0f, 10.0f, 60.0f, spec.maximumX)};
        };
        const auto first = pointcloud::registerRobotBaseFrames(makeFrames(), heightMapOptions());
        pointcloud::IcpOptions cancelledOptions = heightMapOptions();
        cancelledOptions.isCancelled = []() { return true; };
        const auto cancelled = pointcloud::registerRobotBaseFrames(makeFrames(), cancelledOptions);
        if (!require(cancelled.cancelled && !cancelled.ok,
                     "the interleaved cancelled run must stay cancelled")) return 1;
        const auto second = pointcloud::registerRobotBaseFrames(makeFrames(), heightMapOptions());
        if (!require(first.points.size() == second.points.size()
                         && first.cloudIds == second.cloudIds
                         && first.sourceIndices == second.sourceIndices,
                     "repeated 2.5D calls must produce the same mapping")) return 1;
        for (qsizetype index = 0; index < first.registrationCorrections.size(); ++index) {
            for (int element = 0; element < 16; ++element) {
                if (!require(first.registrationCorrections.at(index).constData()[element]
                                 == second.registrationCorrections.at(index).constData()[element],
                             "repeated 2.5D calls must produce the same matrices")) return 1;
            }
        }
        const pointcloud::IcpDiagnostics &a = first.icpDiagnostics[1];
        const pointcloud::IcpDiagnostics &b = second.icpDiagnostics[1];
        if (!require(a.heightMapScoreBefore == b.heightMapScoreBefore
                         && a.heightMapScoreAfter == b.heightMapScoreAfter
                         && a.heightMapAccepted == b.heightMapAccepted
                         && a.selectedRegistrationSource == b.selectedRegistrationSource,
                     "repeated 2.5D calls must produce the same diagnostics")) return 1;
    }

    // (18) Every report field of the iteration contract is serialized into the
    // module diagnostics string the tools, the GUI and the regression harness
    // all consume.
    {
        const HeightMapPair pair = heightMapPair(HeightMapSurface{}, 3.0f, -2.0f);
        const auto result = pointcloud::registerRobotBaseFrames(
            {pair.reference, pair.moving}, heightMapOptions());
        const QStringList required{
            QStringLiteral("heightmap_registration_enabled"),
            QStringLiteral("heightmap_registration_attempted"),
            QStringLiteral("heightmap_registration_accepted"),
            QStringLiteral("heightmap_fallback_to_legacy_icp"),
            QStringLiteral("heightmap_grid_resolution_mm"),
            QStringLiteral("heightmap_valid_ratio_source"),
            QStringLiteral("heightmap_valid_ratio_target"),
            QStringLiteral("heightmap_valid_points_source"),
            QStringLiteral("heightmap_valid_points_target"),
            QStringLiteral("heightmap_score_before"),
            QStringLiteral("heightmap_score_after"),
            QStringLiteral("heightmap_relative_improvement"),
            QStringLiteral("heightmap_delta_xyz_mm"),
            QStringLiteral("heightmap_delta_yaw_deg"),
            QStringLiteral("heightmap_restricted_icp_levels"),
            QStringLiteral("heightmap_restricted_icp_degenerate"),
            QStringLiteral("selected_registration_source"),
            QStringLiteral("heightmap_reject_reason"),
            QStringLiteral("visibility_hard_rejected"),
            QStringLiteral("legacy_icp_comparison")};
        for (const QString &field : required) {
            if (!require(result.diagnostics.contains(field),
                         "the height-map report field must be serialized")) return 1;
        }
        if (!require(result.diagnostics.contains(QStringLiteral("selected_registration_source=heightmap_restricted_icp")),
                     "the report must name the selected registration source")) return 1;
    }


    // (5b) The visibility hard gate wins over the hit-point coverage: a pair
    // whose local accuracy among the matched points is excellent but whose
    // bidirectional hit ratio is below 0.25 must be rejected, and the same
    // fixture must keep the legacy verdict while the switch stays off.
    {
        const DropoutPair pair = lowVisibilityDropoutPair();
        const auto runPair = [](const DropoutPair &input, pointcloud::IcpOptions options) {
            return pointcloud::registerRobotBaseFrames({input.reference, input.moving}, options);
        };
        pointcloud::IcpOptions legacyOptions = dropoutOptions();
        pointcloud::IcpOptions gatedOptions = dropoutOptions();
        gatedOptions.heightMapRegistrationEnabled = true;
        const auto legacyResult = runPair(pair, legacyOptions);
        if (!require(legacyResult.icpDiagnostics.size() == 2,
                     "the low-visibility fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &legacyDiagnostic = legacyResult.icpDiagnostics[1];
        if (!require(legacyDiagnostic.accepted && !legacyDiagnostic.visibilityHardRejected,
                     "with the switch off the legacy verdict must be unchanged")) return 1;
        if (!require(legacyDiagnostic.selectedRegistrationSource.isEmpty(),
                     "with the switch off the 2.5D source field must stay empty")) return 1;
        const auto gatedResult = runPair(pair, gatedOptions);
        if (!require(gatedResult.icpDiagnostics.size() == 2,
                     "the gated fixture must report two diagnostics")) return 1;
        const pointcloud::IcpDiagnostics &gated = gatedResult.icpDiagnostics[1];
        if (!require(gated.structuralBidirectionalMatchRatio < 0.25f,
                     QStringLiteral("the fixture must really stay below the visibility floor: "
                                    "source=%1 target=%2 bidirectional=%3 heightmap_source=%4 "
                                    "heightmap_target=%5 selected=%6 reject=%7")
                         .arg(gated.structuralSourceMatchRatio, 0, 'f', 4)
                         .arg(gated.structuralTargetMatchRatio, 0, 'f', 4)
                         .arg(gated.structuralBidirectionalMatchRatio, 0, 'f', 4)
                         .arg(gated.heightMapSourceValidRatio, 0, 'f', 4)
                         .arg(gated.heightMapTargetValidRatio, 0, 'f', 4)
                         .arg(gated.selectedRegistrationSource, gated.heightMapRejectReason)
                         .toStdString()
                         .c_str())) return 1;
        if (!require(!gated.accepted && gated.visibilityHardRejected,
                     "a low-visibility pair must be hard rejected")) return 1;
        if (!require(gated.failureCode == QStringLiteral("visibility_hard_rejected"),
                     "the hard rejection must carry its own failure code")) return 1;
        if (!require(gated.selectedRegistrationSource == QStringLiteral("none"),
                     "a hard-rejected pair must select no registration source")) return 1;
        if (!require(gated.heightMapRejectReason.isEmpty() || !gated.heightMapAccepted,
                     "a hard-rejected pair must not report an accepted height map")) return 1;
        if (!require(gated.legacyIcpComparisonAvailable,
                     "the legacy baseline must still be recorded after the gate")) return 1;
    }

    std::cout << "Multiframe registration tests passed.\n";
    return 0;
}
