#include <pcv/planefitting/plane_fitting.h>

#include <QVector3D>

#include <cmath>
#include <limits>

namespace pcv::planefitting {

Result fit(const QVector<pointcloud::Point3D> &points,
           const QVector<int> &seedIndices,
           const Options &options)
{
    Result result;
    if (seedIndices.size() < 3 || points.size() < 3) {
        result.error = QStringLiteral("至少需要三个有效采样点");
        return result;
    }
    const auto finite = [](const pointcloud::Point3D &p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    QVector3D origin;
    for (int i = 0; i < seedIndices.size(); ++i) {
        const int index = seedIndices[i];
        if (index < 0 || index >= points.size() || !finite(points[index])) {
            result.error = QStringLiteral("采样点索引无效");
            return result;
        }
        if (seedIndices.indexOf(index) != i) {
            result.error = QStringLiteral("采样点不能重复");
            return result;
        }
        result.controlPoints.push_back(points[index]);
        origin += QVector3D(points[index].x, points[index].y, points[index].z);
    }
    origin /= float(seedIndices.size());
    const QVector3D first(points[seedIndices[0]].x, points[seedIndices[0]].y, points[seedIndices[0]].z);
    const QVector3D second(points[seedIndices[1]].x, points[seedIndices[1]].y, points[seedIndices[1]].z);
    const QVector3D third(points[seedIndices[2]].x, points[seedIndices[2]].y, points[seedIndices[2]].z);
    QVector3D normal = QVector3D::crossProduct(second - first, third - first);
    if (normal.lengthSquared() <= 1.0e-14f) {
        result.error = QStringLiteral("采样点近似共线或距离过近");
        return result;
    }
    normal.normalize();
    if (normal.z() < 0.0f) normal = -normal;
    if (options.useZAxisResidual
        && normal.z() < std::cos(qDegreesToRadians(qBound(0.0f, options.maxNormalTiltDegrees, 89.0f)))) {
        result.error = QStringLiteral("所选平面过于陡峭，不符合 2.5D 高度面约束");
        return result;
    }
    const float d = -QVector3D::dotProduct(normal, first);
    const float initialTolerance = qMax(1.0e-6f, options.initialTolerance);
    const float surfaceTolerance = qMax(1.0e-6f, options.surfaceTolerance);
    const auto residual = [&](const pointcloud::Point3D &p) {
        const float value = normal.x() * p.x + normal.y() * p.y + normal.z() * p.z + d;
        return std::abs(value) / (options.useZAxisResidual ? qMax(std::abs(normal.z()), 1.0e-6f) : 1.0f);
    };
    for (int i = 0; i < points.size(); ++i) {
        if (finite(points[i]) && residual(points[i]) <= initialTolerance) result.candidateIndices.push_back(i);
    }
    const int required = qMax(3, options.minInliers);
    if (result.candidateIndices.size() < required) {
        result.error = QStringLiteral("初始平面附近候选点不足：%1 / %2").arg(result.candidateIndices.size()).arg(required);
        return result;
    }
    for (int seed : seedIndices) if (residual(points[seed]) > surfaceTolerance) {
        result.error = QStringLiteral("最终拟合平面未包含全部控制点");
        return result;
    }
    for (int index : result.candidateIndices) if (residual(points[index]) <= surfaceTolerance) result.planeIndices.push_back(index);
    if (result.planeIndices.size() < required) {
        result.error = QStringLiteral("平面内点不足：%1 / %2").arg(result.planeIndices.size()).arg(required);
        return result;
    }
    double squared = 0.0; float maximum = 0.0f;
    for (int index : result.planeIndices) { result.planePoints.push_back(points[index]); const float distance = residual(points[index]); squared += double(distance) * distance; maximum = qMax(maximum, distance); }
    result.rmsError = float(std::sqrt(squared / qMax(1, result.planeIndices.size())));
    result.usedThreshold = surfaceTolerance;
    result.model = {normal.x(), normal.y(), normal.z(), d, int(result.planeIndices.size()), result.rmsError, maximum};
    result.deferred = options.deferFinalClassification;
    result.ok = true;
    return result;
}

} // namespace pcv::planefitting
