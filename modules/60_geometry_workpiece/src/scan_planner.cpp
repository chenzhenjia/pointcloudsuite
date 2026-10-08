#include <pcv/scan/scan_planner.h>

#include <QJsonArray>

#include <cmath>
#include <limits>

namespace pcv::scan {
namespace {

constexpr float kSingleLineWidthToleranceMm = 0.1f;

bool finite(double value) { return std::isfinite(value); }

bool finite(const QVector3D &value)
{
    return finite(value.x()) && finite(value.y()) && finite(value.z());
}

QJsonArray vectorJson(const QVector3D &value)
{
    return {value.x(), value.y(), value.z()};
}

QJsonArray poseJson(const pointcloud::RobotPose &pose)
{
    return {pose.x, pose.y, pose.z, pose.rx, pose.ry, pose.rz};
}

QString modeName(ScanPlanningMode mode)
{
    return mode == ScanPlanningMode::Global
        ? QStringLiteral("global") : QStringLiteral("local_roi");
}

QString directionName(ScanTravelDirection direction)
{
    return direction == ScanTravelDirection::NegativeYToPositiveY
        ? QStringLiteral("negative_y_to_positive_y")
        : QStringLiteral("positive_y_to_negative_y");
}

QJsonObject pointJson(const ScanTrajectoryPoint &point)
{
    QJsonObject object;
    object.insert(QStringLiteral("point_id"), point.pointId);
    object.insert(QStringLiteral("point_name"), QStringLiteral("P%1").arg(point.pointId));
    object.insert(QStringLiteral("robot_pose"), poseJson(point.robotPose));
    object.insert(QStringLiteral("scan_line_index"), point.scanLineIndex);
    object.insert(QStringLiteral("line_endpoint"),
                  point.lineEndpointStart ? QStringLiteral("start") : QStringLiteral("end"));
    return object;
}

bool validFrame(const pcv::roi::WorkpieceCoordinateSystem &frame)
{
    if (!frame.valid || !finite(frame.originInRobotBase)
        || !finite(frame.axisXInRobotBase) || !finite(frame.axisYInRobotBase)
        || !finite(frame.axisZInRobotBase)
        || !finite(frame.poseA) || !finite(frame.poseB) || !finite(frame.poseC)) return false;
    if (frame.axisXInRobotBase.lengthSquared() <= 1.0e-12f
        || frame.axisYInRobotBase.lengthSquared() <= 1.0e-12f
        || frame.axisZInRobotBase.lengthSquared() <= 1.0e-12f) return false;
    const QVector3D x = frame.axisXInRobotBase.normalized();
    const QVector3D y = frame.axisYInRobotBase.normalized();
    const QVector3D z = frame.axisZInRobotBase.normalized();
    constexpr float kTolerance = 1.0e-3f;
    return std::abs(QVector3D::dotProduct(x, y)) < kTolerance
        && std::abs(QVector3D::dotProduct(x, z)) < kTolerance
        && std::abs(QVector3D::dotProduct(y, z)) < kTolerance
        && QVector3D::dotProduct(QVector3D::crossProduct(x, y), z) > 1.0f - kTolerance;
}

ScanTrajectoryResult fail(const ScanPlanningOptions &options,
                          const QString &code, const QString &message)
{
    ScanTrajectoryResult result;
    result.mode = options.mode;
    result.travelDirection = options.travelDirection;
    result.errorCode = code;
    result.error = message;
    return result;
}

ScanTrajectoryResult cancelled(const ScanPlanningOptions &options)
{
    ScanTrajectoryResult result = fail(options, QStringLiteral("PCV_SCAN_CANCELLED"),
                                       QStringLiteral("扫描路径规划已取消"));
    result.cancelled = true;
    return result;
}

bool cancellationRequested(const ScanPlanningOptions &options)
{
    return options.isCancelled && options.isCancelled();
}

void translatePose(pointcloud::RobotPose *pose, const QVector3D &offset)
{
    pose->x += double(offset.x());
    pose->y += double(offset.y());
    pose->z += double(offset.z());
}

QVector3D toLocal(const QVector3D &robotPoint,
                  const pcv::roi::WorkpieceCoordinateSystem &frame)
{
    const QVector3D relative = robotPoint - frame.originInRobotBase;
    return {QVector3D::dotProduct(relative, frame.axisXInRobotBase.normalized()),
            QVector3D::dotProduct(relative, frame.axisYInRobotBase.normalized()),
            QVector3D::dotProduct(relative, frame.axisZInRobotBase.normalized())};
}

} // namespace

ScanTrajectoryResult planScanTrajectory(const ScanPlanningOptions &options)
{
    if (cancellationRequested(options)) return cancelled(options);
    if (options.mode != ScanPlanningMode::Global && options.mode != ScanPlanningMode::LocalRoi)
        return fail(options, QStringLiteral("PCV_SCAN_MODE_INVALID"),
                    QStringLiteral("扫描规划模式无效"));
    if (options.travelDirection != ScanTravelDirection::PositiveYToNegativeY)
        return fail(options, QStringLiteral("PCV_SCAN_DIRECTION_INVALID"),
                    QStringLiteral("产品扫描方向固定为 WObj1 +Y 到 -Y"));
    if (!validFrame(options.workpieceCoordinateSystem))
        return fail(options, QStringLiteral("PCV_SCAN_FRAME_INVALID"),
                    QStringLiteral("WObj1 坐标系或姿态无效"));
    if (!finite(options.scanHeightMm) || options.scanHeightMm < 0.0f)
        return fail(options, QStringLiteral("PCV_SCAN_HEIGHT_INVALID"),
                    QStringLiteral("扫描高度必须为非负有限值"));
    if (!finite(options.cameraWidthMm) || options.cameraWidthMm <= 0.0f)
        return fail(options, QStringLiteral("PCV_SCAN_CAMERA_WIDTH_INVALID"),
                    QStringLiteral("相机扫描宽度必须大于 0 mm"));
    if (!finite(options.overlapMm) || options.overlapMm < 0.0f
        || options.overlapMm >= options.cameraWidthMm)
        return fail(options, QStringLiteral("PCV_SCAN_OVERLAP_INVALID"),
                    QStringLiteral("重叠距离必须满足 0 <= overlapMm < cameraWidthMm"));
    if (!finite(options.travelExtensionMm) || options.travelExtensionMm < 0.0f)
        return fail(options, QStringLiteral("PCV_SCAN_EXTENSION_INVALID"),
                    QStringLiteral("行程延长距离必须为非负有限值"));

    const QVector<int> &indices = options.mode == ScanPlanningMode::Global
        ? options.globalPlaneIndices : options.localRoiIndices;
    if (indices.isEmpty())
        return fail(options,
                    options.mode == ScanPlanningMode::Global
                        ? QStringLiteral("PCV_SCAN_PLANE_EMPTY")
                        : QStringLiteral("PCV_SCAN_ROI_EMPTY"),
                    options.mode == ScanPlanningMode::Global
                        ? QStringLiteral("全局扫描需要有效拟合平面点")
                        : QStringLiteral("局部扫描需要有效 ROI 点"));

    const auto &frame = options.workpieceCoordinateSystem;
    const QVector3D axisX = frame.axisXInRobotBase.normalized();
    const QVector3D axisY = frame.axisYInRobotBase.normalized();
    const QVector3D axisZ = frame.axisZInRobotBase.normalized();
    float minU = std::numeric_limits<float>::max();
    float maxU = std::numeric_limits<float>::lowest();
    float minV = std::numeric_limits<float>::max();
    float maxV = std::numeric_limits<float>::lowest();
    int validCount = 0;
    for (const int index : indices) {
        if (cancellationRequested(options)) return cancelled(options);
        if (index < 0 || index >= options.points.size())
            return fail(options,
                        options.mode == ScanPlanningMode::Global
                            ? QStringLiteral("PCV_SCAN_PLANE_INDEX_INVALID")
                            : QStringLiteral("PCV_SCAN_ROI_INDEX_INVALID"),
                        QStringLiteral("扫描点索引越界"));
        const pointcloud::Point3D &point = options.points.at(index);
        if (!finite(point.x) || !finite(point.y) || !finite(point.z))
            return fail(options,
                        options.mode == ScanPlanningMode::Global
                            ? QStringLiteral("PCV_SCAN_PLANE_POINT_INVALID")
                            : QStringLiteral("PCV_SCAN_ROI_POINT_INVALID"),
                        QStringLiteral("扫描点包含非有限值"));
        const QVector3D delta(point.x - frame.originInRobotBase.x(),
                              point.y - frame.originInRobotBase.y(),
                              point.z - frame.originInRobotBase.z());
        const float u = QVector3D::dotProduct(delta, axisX);
        const float v = QVector3D::dotProduct(delta, axisY);
        if (!finite(u) || !finite(v))
            return fail(options, QStringLiteral("PCV_SCAN_PROJECTION_INVALID"),
                        QStringLiteral("扫描点无法投影到 WObj1 XY"));
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
        ++validCount;
    }
    if (validCount == 0 || !(maxU > minU) || !(maxV > minV))
        return fail(options, QStringLiteral("PCV_SCAN_RANGE_INVALID"),
                    QStringLiteral("扫描区域在 WObj1 XY 中没有有效范围"));

    const float requiredWidth = maxU - minU;
    const float requiredTravelLength = maxV - minV;
    if (!finite(requiredWidth) || !finite(requiredTravelLength)
        || !(requiredWidth > 0.0f) || !(requiredTravelLength > 0.0f))
        return fail(options, QStringLiteral("PCV_SCAN_RANGE_INVALID"),
                    QStringLiteral("扫描区域投影范围超出有限数值范围"));
    const QVector3D planeCenter = frame.originInRobotBase
        + axisX * ((minU + maxU) * 0.5f)
        + axisY * ((minV + maxV) * 0.5f);
    const QVector3D center = planeCenter + axisZ * options.scanHeightMm;
    if (!finite(planeCenter) || !finite(center))
        return fail(options, QStringLiteral("PCV_SCAN_CENTER_INVALID"),
                    QStringLiteral("扫描中心不是有限值"));
    pointcloud::RobotPose centerPose{center.x(), center.y(), center.z(),
                                     frame.poseA, frame.poseB, frame.poseC};

    if (cancellationRequested(options)) return cancelled(options);
    const double stepTarget = double(options.cameraWidthMm) - double(options.overlapMm);
    const bool fitsSingleLine =
        requiredWidth <= options.cameraWidthMm + kSingleLineWidthToleranceMm;
    int lineCount = 1;
    if (!fitsSingleLine) {
        const double additionalLines = std::ceil(
            (double(requiredWidth) - double(options.cameraWidthMm)) / stepTarget);
        if (!finite(additionalLines)
            || additionalLines > double(std::numeric_limits<int>::max() / 2 - 1))
            return fail(options, QStringLiteral("PCV_SCAN_LINE_COUNT_INVALID"),
                        QStringLiteral("目标区域需要的扫描线数量超出支持范围"));
        lineCount = int(additionalLines) + 1;
    }
    const double spacing = lineCount == 1 ? 0.0
        : (double(requiredWidth) - double(options.cameraWidthMm)) / double(lineCount - 1);
    const double actualOverlap = lineCount == 1
        ? double(options.overlapMm) : double(options.cameraWidthMm) - spacing;
    if (actualOverlap + 1.0e-5 < double(options.overlapMm))
        return fail(options, QStringLiteral("PCV_SCAN_OVERLAP_RESULT_INVALID"),
                    QStringLiteral("实际重叠距离小于请求重叠距离"));
    if (lineCount > kMaxInoRobotPoints / 2)
        return fail(options, QStringLiteral("PCV_SCAN_POINT_LIMIT"),
                    QStringLiteral("扫描点数量超过 P[10] 到 P[9999] 的 9990 点上限"));

    ScanTrajectoryResult result;
    result.ok = true;
    result.mode = options.mode;
    result.travelDirection = options.travelDirection;
    result.centerPose = centerPose;
    result.travelAxisInRobotBase = -axisY;
    result.transverseAxisInRobotBase = axisX;
    result.requiredWidthMm = requiredWidth;
    result.requiredTravelLengthMm = requiredTravelLength;
    result.travelExtensionMm = options.travelExtensionMm;
    result.finalTravelLengthMm = requiredTravelLength + 2.0f * options.travelExtensionMm;
    if (!finite(result.finalTravelLengthMm))
        return fail(options, QStringLiteral("PCV_SCAN_TRAVEL_INVALID"),
                    QStringLiteral("最终扫描行程不是有限值"));
    result.cameraWidthMm = options.cameraWidthMm;
    result.singleLineWidthToleranceMm = kSingleLineWidthToleranceMm;
    result.requestedOverlapMm = options.overlapMm;
    result.actualOverlapMm = float(actualOverlap);
    result.lineSpacingMm = float(spacing);
    result.scanLineCount = lineCount;
    result.points.reserve(lineCount * 2);
    result.segments.reserve(lineCount);

    const float startV = requiredTravelLength * 0.5f + options.travelExtensionMm;
    const float endV = -requiredTravelLength * 0.5f - options.travelExtensionMm;
    const float startY = startV;
    const float endY = endV;
    const float firstX = lineCount == 1
        ? 0.0f : -requiredWidth * 0.5f + options.cameraWidthMm * 0.5f;
    for (int line = 0; line < lineCount; ++line) {
        if (cancellationRequested(options)) return cancelled(options);
        const float x = lineCount == 1 ? 0.0f : firstX + result.lineSpacingMm * float(line);
        const QVector3D startOffset = axisX * x + axisY * startY;
        const QVector3D endOffset = axisX * x + axisY * endY;
        ScanTrajectoryPoint start;
        start.pointId = result.points.size();
        start.robotPose = centerPose;
        translatePose(&start.robotPose, startOffset);
        start.scanLineIndex = line;
        start.lineEndpointStart = true;
        result.points.push_back(start);
        ScanTrajectoryPoint end;
        end.pointId = result.points.size();
        end.robotPose = centerPose;
        translatePose(&end.robotPose, endOffset);
        end.scanLineIndex = line;
        end.lineEndpointStart = false;
        result.points.push_back(end);
        result.segments.push_back({line, start.pointId, end.pointId});
    }
    return result;
}

QJsonObject scanTrajectoryToJson(const ScanTrajectoryResult &result)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("scan_trajectory_v1"));
    root.insert(QStringLiteral("kind"), QStringLiteral("robot_scan_trajectory"));
    root.insert(QStringLiteral("ok"), result.ok);
    root.insert(QStringLiteral("cancelled"), result.cancelled);
    root.insert(QStringLiteral("error_code"), result.errorCode);
    root.insert(QStringLiteral("error"), result.error);
    root.insert(QStringLiteral("mode"), modeName(result.mode));
    root.insert(QStringLiteral("center_pose"), poseJson(result.centerPose));
    QJsonObject requiredArea;
    requiredArea.insert(QStringLiteral("width_mm"), result.requiredWidthMm);
    requiredArea.insert(QStringLiteral("travel_length_mm"), result.requiredTravelLengthMm);
    root.insert(QStringLiteral("required_area"), requiredArea);
    root.insert(QStringLiteral("camera_width_mm"), result.cameraWidthMm);
    root.insert(QStringLiteral("single_line_width_tolerance_mm"),
               result.singleLineWidthToleranceMm);
    root.insert(QStringLiteral("original_travel_length_mm"), result.requiredTravelLengthMm);
    root.insert(QStringLiteral("travel_extension_mm"), result.travelExtensionMm);
    root.insert(QStringLiteral("final_travel_length_mm"), result.finalTravelLengthMm);
    root.insert(QStringLiteral("requested_overlap_mm"), result.requestedOverlapMm);
    root.insert(QStringLiteral("actual_overlap_mm"), result.actualOverlapMm);
    root.insert(QStringLiteral("line_spacing_mm"), result.lineSpacingMm);
    root.insert(QStringLiteral("scan_line_count"), result.scanLineCount);
    root.insert(QStringLiteral("travel_direction"), directionName(result.travelDirection));
    root.insert(QStringLiteral("travel_axis_robot_base"), vectorJson(result.travelAxisInRobotBase));
    root.insert(QStringLiteral("transverse_axis_robot_base"), vectorJson(result.transverseAxisInRobotBase));
    QJsonArray points;
    for (const ScanTrajectoryPoint &point : result.points) points.push_back(pointJson(point));
    root.insert(QStringLiteral("points"), points);
    QJsonArray segments;
    for (const ScanTrajectorySegment &segment : result.segments) {
        QJsonObject item;
        item.insert(QStringLiteral("scan_line_index"), segment.scanLineIndex);
        item.insert(QStringLiteral("start_point_id"), segment.startPointId);
        item.insert(QStringLiteral("end_point_id"), segment.endPointId);
        segments.push_back(item);
    }
    root.insert(QStringLiteral("segments"), segments);
    return root;
}

QJsonObject scanTrajectoryToInoRobotJson(
    const ScanTrajectoryResult &result,
    const pcv::roi::WorkpieceCoordinateSystem &workpieceCoordinateSystem)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("sr2026-p-json-mvp-2"));
    root.insert(QStringLiteral("source_workpiece_info"), QStringLiteral("temp_workpiece_info.json"));
    root.insert(QStringLiteral("wobj_num"), 1);
    root.insert(QStringLiteral("tool_path_num"), result.scanLineCount);
    root.insert(QStringLiteral("points_num"), result.points.size());
    if (!result.ok || result.points.size() > kMaxInoRobotPoints) return root;
    QJsonArray points;
    for (const ScanTrajectoryPoint &point : result.points) {
        const QVector3D local = toLocal(QVector3D(float(point.robotPose.x),
                                                   float(point.robotPose.y),
                                                   float(point.robotPose.z)),
                                        workpieceCoordinateSystem);
        QJsonObject item;
        item.insert(QStringLiteral("number"), point.pointId + 10);
        item.insert(QStringLiteral("pose"), QJsonArray{
            local.x(), local.y(), local.z(), point.robotPose.rz,
            point.robotPose.ry, point.robotPose.rx});
        points.push_back(item);
    }
    root.insert(QStringLiteral("points"), points);
    return root;
}

} // namespace pcv::scan
