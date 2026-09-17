#include <pcv/workpiece/temp_workpiece_geometry.h>

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pcv::roi {
namespace {

bool finite(const QVector3D &v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

QVector3D position(const QVector<pointcloud::Point3D> &points, int index)
{
    const auto &p = points[index];
    return {p.x, p.y, p.z};
}

WorkpieceCoordinateSystem makeFrame(const QVector3D &origin, QVector3D x,
                                    QVector3D yHint)
{
    WorkpieceCoordinateSystem result;
    if (!finite(origin) || !finite(x) || !finite(yHint)
        || x.lengthSquared() <= 1.0e-12f || yHint.lengthSquared() <= 1.0e-12f) {
        result.error = QStringLiteral("O 到 X+/Y+ 的方向长度过小或包含无效坐标");
        return result;
    }
    x.normalize();
    QVector3D z = QVector3D::crossProduct(x, yHint);
    if (z.lengthSquared() <= 1.0e-12f) {
        result.error = QStringLiteral("O/X+/Y+ 三点近似共线");
        return result;
    }
    z.normalize();
    QVector3D y = QVector3D::crossProduct(z, x);
    if (y.lengthSquared() <= 1.0e-12f) {
        result.error = QStringLiteral("WObj1 Y 轴计算失败");
        return result;
    }
    y.normalize();
    if (QVector3D::dotProduct(y, yHint) < 0.0f) {
        y = -y;
        z = -z;
    }
    result.originInRobotBase = origin;
    result.axisXInRobotBase = x;
    result.axisYInRobotBase = y;
    result.axisZInRobotBase = z;
    result.workpieceToRobotBase.setToIdentity();
    for (int row = 0; row < 3; ++row) {
        result.workpieceToRobotBase(row, 0) = x[row];
        result.workpieceToRobotBase(row, 1) = y[row];
        result.workpieceToRobotBase(row, 2) = z[row];
        result.workpieceToRobotBase(row, 3) = origin[row];
    }
    bool invertible = false;
    result.robotBaseToWorkpiece = result.workpieceToRobotBase.inverted(&invertible);
    if (!invertible) {
        result.error = QStringLiteral("WObj1 坐标系矩阵不可逆");
        return result;
    }
    result.orthogonalityError = std::max({std::abs(QVector3D::dotProduct(x, y)),
                                          std::abs(QVector3D::dotProduct(x, z)),
                                          std::abs(QVector3D::dotProduct(y, z))});
    const double b = std::asin(std::clamp(-double(result.workpieceToRobotBase(2, 0)), -1.0, 1.0));
    const double cb = std::cos(b);
    double a = 0.0, c = 0.0;
    if (std::abs(cb) > 1.0e-8) {
        a = std::atan2(double(result.workpieceToRobotBase(2, 1)), double(result.workpieceToRobotBase(2, 2)));
        c = std::atan2(double(result.workpieceToRobotBase(1, 0)), double(result.workpieceToRobotBase(0, 0)));
    } else {
        a = std::atan2(-double(result.workpieceToRobotBase(0, 1)), double(result.workpieceToRobotBase(1, 1)));
    }
    result.poseA = float(qRadiansToDegrees(a));
    result.poseB = float(qRadiansToDegrees(b));
    result.poseC = float(qRadiansToDegrees(c));
    result.valid = true;
    return result;
}

} // namespace

AxisPointSelection selectAutomaticAxisPoints(const QVector<pointcloud::Point3D> &points,
                                              const QVector<int> &planeIndices,
                                              const QVector3D &origin,
                                              const QVector3D &planeNormal,
                                              float planeToleranceMm)
{
    AxisPointSelection result;
    QVector3D z = planeNormal;
    if (!finite(origin) || !finite(z) || z.lengthSquared() <= 1.0e-10f) {
        result.error = QStringLiteral("拟合平面法向量或原点无效");
        return result;
    }
    z.normalize();
    if (z.z() < 0.0f) z = -z;
    QVector3D x = QVector3D(1, 0, 0) - z * QVector3D::dotProduct(QVector3D(1, 0, 0), z);
    if (x.lengthSquared() <= 1.0e-10f) {
        result.error = QStringLiteral("机器人基坐标 X 轴投影退化");
        return result;
    }
    x.normalize();
    QVector3D y = QVector3D::crossProduct(z, x).normalized();
    if (QVector3D::dotProduct(y, QVector3D(0, 1, 0)) < 0.0f) { x = -x; y = -y; }
    QVector<int> valid;
    float minU = std::numeric_limits<float>::max(), maxU = std::numeric_limits<float>::lowest();
    float minV = minU, maxV = maxU;
    for (int index : planeIndices) {
        if (index < 0 || index >= points.size()) continue;
        const QVector3D delta = position(points, index) - origin;
        if (!finite(delta) || std::abs(QVector3D::dotProduct(delta, z)) > std::max(0.0f, planeToleranceMm)) continue;
        const float u = QVector3D::dotProduct(delta, x), v = QVector3D::dotProduct(delta, y);
        if (!std::isfinite(u) || !std::isfinite(v)) continue;
        valid.push_back(index); minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
    }
    if (valid.isEmpty() || std::min(maxU - minU, maxV - minV) <= 0.0f) {
        result.error = QStringLiteral("最终平面范围不足，无法自动取辅助点");
        return result;
    }
    result.targetDistanceMm = std::clamp(0.25f * std::min(maxU - minU, maxV - minV), 5.0f, 50.0f);
    const auto choose = [&](const QVector3D &axis, int forbidden, int *selected, float *actual, bool *fallback) {
        int target = -1, farthest = -1; float targetError = std::numeric_limits<float>::max(); float maxProjection = 0.0f;
        for (int index : valid) {
            if (index == forbidden) continue;
            const QVector3D delta = position(points, index) - origin;
            const float projection = QVector3D::dotProduct(delta, axis);
            if (projection <= 1.0e-5f) continue;
            if (projection > maxProjection) { maxProjection = projection; farthest = index; }
            if (projection >= result.targetDistanceMm) {
                const float e = (delta - axis * result.targetDistanceMm).lengthSquared();
                if (e < targetError) { targetError = e; target = index; }
            }
        }
        *selected = target >= 0 ? target : farthest; *fallback = target < 0;
        if (*selected >= 0) *actual = (position(points, *selected) - origin).length();
    };
    choose(x, -1, &result.xPointIndex, &result.xActualDistanceMm, &result.xUsedFallback);
    choose(y, result.xPointIndex, &result.yPointIndex, &result.yActualDistanceMm, &result.yUsedFallback);
    result.ok = result.xPointIndex >= 0 && result.yPointIndex >= 0;
    if (!result.ok) result.error = QStringLiteral("机器人基坐标正 X/Y 方向缺少可用平面点");
    return result;
}

WorkpieceCoordinateSystem buildWorkpieceCoordinateSystemFromThreePoints(
    const QVector<pointcloud::Point3D> &points, const QVector3D &origin,
    int xAxisPointIndex, int yAxisPointIndex)
{
    if (xAxisPointIndex < 0 || yAxisPointIndex < 0 || xAxisPointIndex >= points.size() || yAxisPointIndex >= points.size()) {
        WorkpieceCoordinateSystem result; result.error = QStringLiteral("X+/Y+ 点索引无效"); return result;
    }
    if (xAxisPointIndex == yAxisPointIndex) {
        WorkpieceCoordinateSystem result; result.error = QStringLiteral("X+/Y+ 必须使用两个不同的点"); return result;
    }
    return makeFrame(origin, position(points, xAxisPointIndex) - origin,
                     position(points, yAxisPointIndex) - origin);
}

PlaneMaskResult rasterizePlaneMask(const QVector<pointcloud::Point3D> &points,
                                   const QVector<int> &planeIndices,
                                   const QVector4D &planeModel,
                                   const WorkpieceCoordinateSystem &frame,
                                   float widthMm, float heightMm, float pixelSizeMm,
                                   const QVector<int> &explicitRoiIndices,
                                   float planeDistanceToleranceMm,
                                   const std::function<bool()> &isCancelled)
{
    PlaneMaskResult result;
    if (!frame.valid || !std::isfinite(widthMm) || !std::isfinite(heightMm)
        || widthMm <= 0.0f || heightMm <= 0.0f || pixelSizeMm <= 0.0f
        || !std::isfinite(planeDistanceToleranceMm) || planeDistanceToleranceMm < 0.0f) {
        result.error = QStringLiteral("ROI 矩形尺寸或 WObj1 无效"); return result;
    }
    const int width = qBound(1, int(std::ceil(widthMm / pixelSizeMm)), 20000);
    const int height = qBound(1, int(std::ceil(heightMm / pixelSizeMm)), 20000);
    result.image = QImage(width, height, QImage::Format_Grayscale8);
    result.image.fill(0);
    const QVector<int> source = explicitRoiIndices.isEmpty() ? planeIndices : explicitRoiIndices;
    const QVector3D normal(planeModel.x(), planeModel.y(), planeModel.z());
    const float normalLength = normal.length();
    if (!std::isfinite(normalLength) || normalLength <= 1.0e-10f) {
        result.error = QStringLiteral("平面模型无效");
        return result;
    }
    for (int index : source) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            result.error = QStringLiteral("cancelled");
            return result;
        }
        if (index < 0 || index >= points.size()) continue;
        const auto &p = points[index];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        const QVector3D delta = position(points, index) - frame.originInRobotBase;
        const float planeDistance = QVector3D::dotProduct(normal, position(points, index)) + planeModel.w();
        if (std::abs(planeDistance) / normalLength > planeDistanceToleranceMm) continue;
        const float u = QVector3D::dotProduct(delta, frame.axisXInRobotBase);
        const float v = QVector3D::dotProduct(delta, frame.axisYInRobotBase);
        if (u < -widthMm * 0.5f || u > widthMm * 0.5f || v < -heightMm * 0.5f || v > heightMm * 0.5f) continue;
        const int px = qBound(0, int(std::floor((u + widthMm * 0.5f) / pixelSizeMm)), width - 1);
        const int py = qBound(0, height - 1 - int(std::floor((v + heightMm * 0.5f) / pixelSizeMm)), height - 1);
        result.image.setPixel(px, py, 255);
        result.roiIndices.push_back(index);
    }
    if (result.roiIndices.isEmpty()) { result.error = QStringLiteral("ROI 没有落入矩形的有效点"); return result; }
    result.ok = true;
    return result;
}

} // namespace pcv::roi
