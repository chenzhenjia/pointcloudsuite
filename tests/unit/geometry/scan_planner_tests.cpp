#include <pcv/scan/scan_planner.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool require(bool value, const char *message) { if (!value) std::cerr << message << '\n'; return value; }
bool near(double actual, double expected, double tolerance = 1.0e-4) { return std::abs(actual - expected) <= tolerance; }

pcv::roi::WorkpieceCoordinateSystem frame() {
    pcv::roi::WorkpieceCoordinateSystem value;
    value.originInRobotBase = {100.0f, 200.0f, 300.0f};
    value.axisXInRobotBase = {1.0f, 0.0f, 0.0f};
    value.axisYInRobotBase = {0.0f, 1.0f, 0.0f};
    value.axisZInRobotBase = {0.0f, 0.0f, 1.0f};
    value.poseA = 10.0f; value.poseB = 20.0f; value.poseC = 30.0f; value.valid = true;
    return value;
}

pcv::scan::ScanPlanningOptions options(pcv::scan::ScanPlanningMode mode) {
    pcv::scan::ScanPlanningOptions value;
    value.mode = mode; value.workpieceCoordinateSystem = frame();
    value.points = {{80.0f, 180.0f, 300.0f}, {120.0f, 220.0f, 300.0f}};
    value.globalPlaneIndices = {0, 1}; value.localRoiIndices = {0, 1};
    value.scanHeightMm = 50.0f; value.cameraWidthMm = 100.0f; value.overlapMm = 50.0f; value.travelExtensionMm = 50.0f;
    value.travelDirection = pcv::scan::ScanTravelDirection::PositiveYToNegativeY;
    return value;
}

bool testGlobalAndLocalBounds() {
    for (const auto mode : {pcv::scan::ScanPlanningMode::Global, pcv::scan::ScanPlanningMode::LocalRoi}) {
        const auto result = pcv::scan::planScanTrajectory(options(mode));
        if (!require(result.ok && result.mode == mode, "mode planning failed")) return false;
        if (!require(near(result.centerPose.x, 100.0) && near(result.centerPose.y, 200.0) && near(result.centerPose.z, 350.0), "bbox center/height failed")) return false;
        if (!require(near(result.requiredWidthMm, 40.0) && near(result.requiredTravelLengthMm, 40.0), "bbox range failed")) return false;
        if (!require(result.scanLineCount == 1 && result.points.size() == 2, "camera covering width must create one line")) return false;
        if (!require(near(result.points.at(0).robotPose.y, 270.0) && near(result.points.at(1).robotPose.y, 130.0), "default direction/extension failed")) return false;
        if (!require(near(result.points.at(0).robotPose.rx, 10.0)
                     && near(result.points.at(0).robotPose.ry, 20.0)
                     && near(result.points.at(0).robotPose.rz, 30.0),
                     "WObj1 ABC inheritance failed")) return false;
    }
    return true;
}

bool testMultipleLinesAndCoverage() {
    auto input = options(pcv::scan::ScanPlanningMode::Global); input.cameraWidthMm = 20.0f; input.overlapMm = 10.0f;
    const auto result = pcv::scan::planScanTrajectory(input);
    if (!require(result.ok && result.scanLineCount == 3 && result.points.size() == 6, "multi-line count failed")) return false;
    if (!require(near(result.lineSpacingMm, 10.0) && near(result.actualOverlapMm, 10.0) && near(result.requestedOverlapMm, 10.0), "overlap failed")) return false;
    if (!require(near(result.points.first().robotPose.x, 90.0) && near(result.points.at(4).robotPose.x, 110.0), "coverage edges failed")) return false;
    if (!require(near(result.points.first().robotPose.y, 270.0)
                 && near(result.points.at(1).robotPose.y, 130.0)
                 && near(result.finalTravelLengthMm, 140.0), "fixed Y extension failed")) return false;
    if (!require(result.actualOverlapMm >= result.requestedOverlapMm,
                 "actual overlap fell below requested overlap")) return false;
    for (int line = 0; line < result.scanLineCount; ++line) {
        const auto &start = result.points.at(line * 2); const auto &end = result.points.at(line * 2 + 1);
        if (!require(start.pointId == line * 2 && end.pointId == line * 2 + 1 && start.scanLineIndex == line && end.scanLineIndex == line && start.robotPose.y > end.robotPose.y, "numbering/direction failed")) return false;
        if (line > 0 && !require(start.robotPose.x > result.points.at((line - 1) * 2).robotPose.x, "-X to +X numbering failed")) return false;
    }
    return true;
}

bool testWidthBoundariesAndCustomOverlap() {
    auto input = options(pcv::scan::ScanPlanningMode::LocalRoi);
    if (!require(input.mode == pcv::scan::ScanPlanningMode::LocalRoi
                 && near(input.scanHeightMm, 50.0) && near(input.cameraWidthMm, 100.0)
                 && near(input.overlapMm, 50.0), "default scan parameters failed")) return false;
    const auto planWidth = [](float width, float camera, float overlap) {
        auto value = options(pcv::scan::ScanPlanningMode::LocalRoi);
        value.workpieceCoordinateSystem.originInRobotBase = {};
        value.points = {{0.0f, -20.0f, 0.0f}, {width, 20.0f, 0.0f}};
        value.localRoiIndices = {0, 1}; value.cameraWidthMm = camera; value.overlapMm = overlap;
        return pcv::scan::planScanTrajectory(value);
    };
    const auto below = planWidth(99.9f, 100.0f, 50.0f);
    const auto equal = planWidth(100.0f, 100.0f, 50.0f);
    const auto nearTolerance = planWidth(100.099f, 100.0f, 50.0f);
    const auto toleranceBoundary = planWidth(100.1f, 100.0f, 50.0f);
    const auto aboveTolerance = planWidth(100.1001f, 100.0f, 50.0f);
    const auto above = planWidth(101.0f, 100.0f, 50.0f);
    if (!require(below.ok && below.points.size() == 2 && equal.ok && equal.points.size() == 2,
                 "width <= camera must create one line")) return false;
    if (!require(nearTolerance.ok && nearTolerance.scanLineCount == 1 && nearTolerance.points.size() == 2
                 && toleranceBoundary.ok && toleranceBoundary.scanLineCount == 1
                 && toleranceBoundary.points.size() == 2,
                 "single-line tolerance boundary failed")) return false;
    if (!require(aboveTolerance.ok && aboveTolerance.scanLineCount == 2
                 && aboveTolerance.points.size() == 4,
                 "width above single-line tolerance must create multiple lines")) return false;
    if (!require(above.ok && above.points.size() == 4 && near(above.lineSpacingMm, 1.0)
                 && near(above.actualOverlapMm, 99.0), "width > camera coverage calculation failed")) return false;
    const auto custom = planWidth(230.0f, 100.0f, 30.0f);
    if (!require(custom.ok && custom.scanLineCount == 3 && near(custom.lineSpacingMm, 65.0)
                 && near(custom.actualOverlapMm, 35.0), "custom overlap calculation failed")) return false;
    auto invalid = options(pcv::scan::ScanPlanningMode::LocalRoi); invalid.cameraWidthMm = 0.0f;
    if (!require(!pcv::scan::planScanTrajectory(invalid).ok, "nonpositive width accepted")) return false;
    invalid = options(pcv::scan::ScanPlanningMode::LocalRoi); invalid.overlapMm = -1.0f;
    if (!require(!pcv::scan::planScanTrajectory(invalid).ok, "negative overlap accepted")) return false;
    invalid.overlapMm = invalid.cameraWidthMm;
    return require(!pcv::scan::planScanTrajectory(invalid).ok, "overlap >= width accepted");
}

bool testSingleLineToleranceAcrossModes() {
    for (const auto mode : {pcv::scan::ScanPlanningMode::Global, pcv::scan::ScanPlanningMode::LocalRoi}) {
        for (const float width : {99.9f, 100.0f, 100.099f, 100.1f, 100.1001f, 101.0f}) {
            auto input = options(mode);
            input.workpieceCoordinateSystem.originInRobotBase = {};
            input.points = {{0.0f, -20.0f, 0.0f}, {width, 20.0f, 0.0f}};
            const auto result = pcv::scan::planScanTrajectory(input);
            const int expectedLines = width <= 100.1f ? 1 : 2;
            if (!require(result.ok && result.scanLineCount == expectedLines
                         && result.points.size() == expectedLines * 2
                         && result.segments.size() == expectedLines,
                         "tolerance line/point/segment count failed")) return false;
            if (!require(result.requiredWidthMm == width
                         && near(result.singleLineWidthToleranceMm, 0.1),
                         "original width or tolerance diagnostic changed")) return false;
            if (!require(near(result.centerPose.x, width * 0.5)
                         && near(result.centerPose.z, 50.0)
                         && near(result.points.at(0).robotPose.y, 70.0)
                         && near(result.points.at(1).robotPose.y, -70.0),
                         "tolerance changed center/height/travel")) return false;
            const QJsonObject internal = pcv::scan::scanTrajectoryToJson(result);
            const QJsonObject ino = pcv::scan::scanTrajectoryToInoRobotJson(
                result, input.workpieceCoordinateSystem);
            const QJsonArray points = ino.value(QStringLiteral("points")).toArray();
            if (!require(internal.value(QStringLiteral("segments")).toArray().size() == expectedLines
                         && near(internal.value(QStringLiteral("single_line_width_tolerance_mm")).toDouble(), 0.1)
                         && !ino.contains(QStringLiteral("single_line_width_tolerance_mm"))
                         && ino.value(QStringLiteral("tool_path_num")).toInt() == expectedLines
                         && ino.value(QStringLiteral("points_num")).toInt() == expectedLines * 2
                         && points.size() == expectedLines * 2
                         && points.first().toObject().value(QStringLiteral("number")).toInt() == 10
                         && points.last().toObject().value(QStringLiteral("number")).toInt() == 9 + expectedLines * 2,
                         "tolerance JSON count/numbering failed")) return false;
            if (expectedLines == 1) {
                if (!require(result.points.last().pointId == 1
                             && result.segments.first().startPointId == 0
                             && result.segments.first().endPointId == 1
                             && near(result.points.first().robotPose.x, width * 0.5)
                             && near(result.points.last().robotPose.x, width * 0.5)
                             && near(result.lineSpacingMm, 0.0)
                             && near(result.actualOverlapMm, input.overlapMm),
                             "single line must contain only centered P0/P1")) return false;
            } else {
                if (!require(near(result.points.first().robotPose.x - 50.0, 0.0)
                             && near(result.points.at(2).robotPose.x + 50.0, width)
                             && near(result.lineSpacingMm, width - 100.0)
                             && result.actualOverlapMm >= result.requestedOverlapMm,
                             "above-tolerance coverage/spacing failed")) return false;
            }
        }
    }
    return true;
}

bool testInoRobotPointLimit() {
    auto input = options(pcv::scan::ScanPlanningMode::LocalRoi);
    input.points = {{-1.0f, 0.0f, 0.0f}, {499500.0f, 20.0f, 0.0f}};
    input.localRoiIndices = {0, 1}; input.cameraWidthMm = 100.0f; input.overlapMm = 50.0f;
    const auto result = pcv::scan::planScanTrajectory(input);
    if (!require(!result.ok && result.errorCode == QStringLiteral("PCV_SCAN_POINT_LIMIT"),
                 "P.json point limit was not enforced")) return false;
    const QJsonObject json = pcv::scan::scanTrajectoryToInoRobotJson(result, input.workpieceCoordinateSystem);
    if (!require(json.value(QStringLiteral("points")).toArray().isEmpty(),
                 "failed planning must not serialize partial P.json points")) return false;
    input.points = {{-1.0f, 0.0f, 0.0f}, {249799.0f, 20.0f, 0.0f}};
    const auto boundary = pcv::scan::planScanTrajectory(input);
    if (!require(boundary.ok && boundary.points.size() == pcv::scan::kMaxInoRobotPoints,
                 "9990 point P.json boundary must be accepted")) return false;
    const QJsonObject boundaryJson = pcv::scan::scanTrajectoryToInoRobotJson(
        boundary, input.workpieceCoordinateSystem);
    const QJsonArray points = boundaryJson.value(QStringLiteral("points")).toArray();
    return require(points.size() == 9990
                   && points.first().toObject().value(QStringLiteral("number")).toInt() == 10
                   && points.last().toObject().value(QStringLiteral("number")).toInt() == 9999,
                   "P.json point number bounds failed");
}

bool testForwardDirectionAndTiltedHeight() {
    auto input = options(pcv::scan::ScanPlanningMode::LocalRoi); input.cameraWidthMm = 20.0f;
    input.travelDirection = pcv::scan::ScanTravelDirection::NegativeYToPositiveY;
    const auto result = pcv::scan::planScanTrajectory(input);
    if (!require(!result.ok && result.errorCode == QStringLiteral("PCV_SCAN_DIRECTION_INVALID"), "non-product direction accepted")) return false;
    constexpr float k = 0.70710678118f;
    input = options(pcv::scan::ScanPlanningMode::LocalRoi);
    input.points = {{100.0f, 200.0f, 300.0f}, {100.0f + 20.0f * k, 220.0f, 300.0f + 20.0f * k}};
    input.localRoiIndices = {0, 1}; input.workpieceCoordinateSystem.axisXInRobotBase = {k, 0.0f, k}; input.workpieceCoordinateSystem.axisYInRobotBase = {0.0f, 1.0f, 0.0f}; input.workpieceCoordinateSystem.axisZInRobotBase = {-k, 0.0f, k}; input.scanHeightMm = 50.0f; input.cameraWidthMm = 100.0f;
    const auto tilted = pcv::scan::planScanTrajectory(input);
    if (!require(tilted.ok, "tilted frame planning failed")
        || !require(near(tilted.centerPose.x, 100.0 - 40.0 * k)
                    && near(tilted.centerPose.y, 210.0)
                    && near(tilted.centerPose.z, 300.0 + 60.0 * k),
                    "tilted bbox center and height mapping failed")) return false;
    const QJsonObject json = pcv::scan::scanTrajectoryToInoRobotJson(
        tilted, input.workpieceCoordinateSystem);
    const QJsonArray points = json.value(QStringLiteral("points")).toArray();
    if (!require(points.size() == 2, "tilted P.json point count failed")) return false;
    const QJsonArray pose = points.at(0).toObject().value(QStringLiteral("pose")).toArray();
    return require(near(pose.at(0).toDouble(), 10.0, 1.0e-3)
                   && near(pose.at(1).toDouble(), 70.0, 1.0e-3)
                   && near(pose.at(2).toDouble(), 50.0, 1.0e-3),
                   "tilted WObj1 local XYZ mapping failed");
}

bool testInvalidInputsAndCancellation() {
    auto input = options(pcv::scan::ScanPlanningMode::Global); input.cameraWidthMm = 0.0f;
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid camera width accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::Global); input.scanHeightMm = -1.0f;
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid height accepted")) return false;
    input.workpieceCoordinateSystem.poseA = std::numeric_limits<float>::quiet_NaN();
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid WObj1 pose accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::Global); input.globalPlaneIndices.clear();
    if (!require(pcv::scan::planScanTrajectory(input).errorCode == QStringLiteral("PCV_SCAN_PLANE_EMPTY"), "empty plane was not rejected")) return false;
    input.globalPlaneIndices = {4}; if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid plane index accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::LocalRoi); input.localRoiIndices = {4};
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid ROI index accepted")) return false;
    input.localRoiIndices.clear();
    if (!require(pcv::scan::planScanTrajectory(input).errorCode == QStringLiteral("PCV_SCAN_ROI_EMPTY"), "empty ROI was not rejected")) return false;
    input.localRoiIndices = {0}; input.points[0].x = std::numeric_limits<float>::infinity();
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "non-finite ROI point accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::LocalRoi);
    input.workpieceCoordinateSystem.axisXInRobotBase = {};
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "invalid WObj1 axes accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::LocalRoi);
    input.points = {{100.0f, 200.0f, 300.0f}, {100.0f, 220.0f, 300.0f}};
    input.localRoiIndices = {0, 1};
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "zero projected ROI width accepted")) return false;
    input.points = {{100.0f, 200.0f, 300.0f}, {120.0f, 200.0f, 300.0f}};
    if (!require(!pcv::scan::planScanTrajectory(input).ok, "zero projected ROI travel accepted")) return false;
    input = options(pcv::scan::ScanPlanningMode::LocalRoi); input.isCancelled = [] { return true; };
    const auto cancelled = pcv::scan::planScanTrajectory(input);
    return require(!cancelled.ok && cancelled.cancelled && cancelled.errorCode == QStringLiteral("PCV_SCAN_CANCELLED"), "cancelled planning published success");
}

bool testJsonContracts() {
    auto input = options(pcv::scan::ScanPlanningMode::Global); input.cameraWidthMm = 20.0f; input.overlapMm = 10.0f;
    const auto result = pcv::scan::planScanTrajectory(input); if (!require(result.ok, "JSON source planning failed")) return false;
    const QJsonObject json = pcv::scan::scanTrajectoryToJson(result); const QJsonObject area = json.value(QStringLiteral("required_area")).toObject(); const QJsonArray points = json.value(QStringLiteral("points")).toArray();
    if (!require(json.value(QStringLiteral("mode")).toString() == QStringLiteral("global") && json.value(QStringLiteral("schema_version")).toString() == QStringLiteral("scan_trajectory_v1") && area.value(QStringLiteral("width_mm")).toDouble() == 40.0 && points.at(0).toObject().value(QStringLiteral("point_name")).toString() == QStringLiteral("P0") && near(json.value(QStringLiteral("single_line_width_tolerance_mm")).toDouble(), 0.1), "internal JSON contract failed")) return false;
    const QJsonObject ino = pcv::scan::scanTrajectoryToInoRobotJson(result, input.workpieceCoordinateSystem); const QJsonArray inoPoints = ino.value(QStringLiteral("points")).toArray(); const QJsonArray pose = inoPoints.at(0).toObject().value(QStringLiteral("pose")).toArray();
    return require(ino.value(QStringLiteral("schema_version")).toString() == QStringLiteral("sr2026-p-json-mvp-2") && ino.value(QStringLiteral("source_workpiece_info")).toString() == QStringLiteral("temp_workpiece_info.json") && ino.value(QStringLiteral("wobj_num")).toInt() == 1 && !ino.contains(QStringLiteral("wobj")) && ino.value(QStringLiteral("tool_path_num")).toInt() == result.scanLineCount && ino.value(QStringLiteral("points_num")).toInt() == result.points.size() && inoPoints.at(0).toObject().value(QStringLiteral("number")).toInt() == 10 && inoPoints.at(1).toObject().value(QStringLiteral("number")).toInt() == 11 && pose.size() == 6 && near(pose.at(0).toDouble(), -10.0) && near(pose.at(1).toDouble(), 70.0) && near(pose.at(2).toDouble(), 50.0) && near(pose.at(3).toDouble(), result.points.at(0).robotPose.rz) && near(pose.at(5).toDouble(), result.points.at(0).robotPose.rx), "InoRobot JSON contract failed");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!testGlobalAndLocalBounds() || !testMultipleLinesAndCoverage() || !testWidthBoundariesAndCustomOverlap() || !testSingleLineToleranceAcrossModes() || !testInoRobotPointLimit() || !testForwardDirectionAndTiltedHeight() || !testInvalidInputsAndCancellation() || !testJsonContracts()) return 1;
    std::cout << "scan_planner_tests: PASS\n";
    return 0;
}
