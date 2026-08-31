#include <pcv/registration/seam_fusion.h>

#include <QCoreApplication>

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
    const auto fused = pointcloud::applyTrajectorySeamFusion(&valid, enabled);
    if (!require(fused.ok && fused.outputPoints > 0 && !fused.diagnostics.isEmpty()
                 && fused.diagnostics.first().actualOverlapValid
                 && fused.diagnostics.first().applied,
                 "valid overlapping frames must produce seam diagnostics and fused points")) return 1;

    std::cout << "Seam fusion safety tests passed.\n";
    return 0;
}
