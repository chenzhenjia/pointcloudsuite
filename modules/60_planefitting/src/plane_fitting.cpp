#include <pcv/planefitting/plane_fitting.h>

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace {

void symmetricEigen3(const double input[3][3], double values[3], double vectors[3][3])
{
    double a[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            a[r][c] = input[r][c];
            vectors[r][c] = r == c ? 1.0 : 0.0;
        }
    for (int iteration = 0; iteration < 24; ++iteration) {
        int p = 0;
        int q = 1;
        double largest = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > largest) { p = 0; q = 2; largest = std::abs(a[0][2]); }
        if (std::abs(a[1][2]) > largest) { p = 1; q = 2; largest = std::abs(a[1][2]); }
        if (largest < 1.0e-12) break;
        const double angle = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double cs = std::cos(angle);
        const double sn = std::sin(angle);
        for (int k = 0; k < 3; ++k) {
            const double apk = a[p][k], aqk = a[q][k];
            a[p][k] = cs * apk - sn * aqk;
            a[q][k] = sn * apk + cs * aqk;
        }
        for (int k = 0; k < 3; ++k) {
            const double akp = a[k][p], akq = a[k][q];
            a[k][p] = cs * akp - sn * akq;
            a[k][q] = sn * akp + cs * akq;
            const double vkp = vectors[k][p], vkq = vectors[k][q];
            vectors[k][p] = cs * vkp - sn * vkq;
            vectors[k][q] = sn * vkp + cs * vkq;
        }
    }
    values[0] = a[0][0]; values[1] = a[1][1]; values[2] = a[2][2];
    for (int i = 0; i < 3; ++i) {
        int best = i;
        for (int j = i + 1; j < 3; ++j)
            if (values[j] > values[best]) best = j;
        if (best == i) continue;
        std::swap(values[i], values[best]);
        for (int r = 0; r < 3; ++r) std::swap(vectors[r][i], vectors[r][best]);
    }
}

struct Cell {
    int x = 0;
    int y = 0;
    bool operator==(const Cell &other) const { return x == other.x && y == other.y; }
};
struct CellHash {
    std::size_t operator()(const Cell &cell) const noexcept {
        return std::hash<int>{}(cell.x) ^ (std::hash<int>{}(cell.y) << 1);
    }
};

} // namespace

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
    const auto vectorFor = [&points](int index) {
        const auto &p = points[index];
        return QVector3D(p.x, p.y, p.z);
    };
    const QVector3D first = vectorFor(seedIndices[0]);
    double covariance[3][3] = {};
    for (int seed : seedIndices) {
        const QVector3D delta = vectorFor(seed) - origin;
        const double values[3] = {delta.x(), delta.y(), delta.z()};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) covariance[r][c] += values[r] * values[c];
    }
    double eigenvalues[3] = {};
    double eigenvectors[3][3] = {};
    symmetricEigen3(covariance, eigenvalues, eigenvectors);
    if (!std::isfinite(eigenvalues[0]) || !std::isfinite(eigenvalues[1])
        || eigenvalues[1] <= qMax(eigenvalues[0], 1.0e-20) * 1.0e-6) {
        result.error = QStringLiteral("采样点近似共线或距离过近");
        return result;
    }
    QVector3D normal{float(eigenvectors[0][2]), float(eigenvectors[1][2]), float(eigenvectors[2][2])};
    if (normal.lengthSquared() <= 1.0e-14f) { result.error = QStringLiteral("采样点无法确定初始平面"); return result; }
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
    const auto residualFor = [&](const QVector3D &modelNormal, float modelD,
                                 const pointcloud::Point3D &p) {
        const float value = modelNormal.x() * p.x + modelNormal.y() * p.y + modelNormal.z() * p.z + modelD;
        return std::abs(value) / (options.useZAxisResidual ? qMax(std::abs(modelNormal.z()), 1.0e-6f) : 1.0f);
    };
    for (int i = 0; i < points.size(); ++i) {
        if (finite(points[i]) && residualFor(normal, d, points[i]) <= initialTolerance) result.candidateIndices.push_back(i);
    }
    const int required = qMax(3, options.minInliers);
    if (result.candidateIndices.size() < required) {
        result.error = QStringLiteral("初始平面附近候选点不足：%1 / %2").arg(result.candidateIndices.size()).arg(required);
        return result;
    }
    QVector<int> modelCandidates;
    const int maximumModelPoints = 20000;
    const int step = qMax(1, int((result.candidateIndices.size() + maximumModelPoints - 1) / maximumModelPoints));
    for (int i = 0; i < result.candidateIndices.size(); i += step) modelCandidates.push_back(result.candidateIndices[i]);
    QVector<int> bestModelInliers;
    QVector3D bestNormal = normal;
    float bestD = d;
    std::mt19937 rng(options.randomSeed);
    std::uniform_int_distribution<int> pick(0, modelCandidates.size() - 1);
    const float minimumNormalZ = std::cos(qDegreesToRadians(qBound(0.0f, options.maxNormalTiltDegrees, 89.0f)));
    for (int iteration = 0; iteration < qMax(1, options.ransacIterations); ++iteration) {
        const int ia = pick(rng), ib = pick(rng), ic = pick(rng);
        if (ia == ib || ia == ic || ib == ic) continue;
        QVector3D candidateNormal = QVector3D::crossProduct(vectorFor(modelCandidates[ib]) - vectorFor(modelCandidates[ia]), vectorFor(modelCandidates[ic]) - vectorFor(modelCandidates[ia]));
        if (candidateNormal.lengthSquared() <= 1.0e-14f) continue;
        candidateNormal.normalize();
        if (QVector3D::dotProduct(candidateNormal, normal) < 0.0f) candidateNormal = -candidateNormal;
        if (options.useZAxisResidual && candidateNormal.z() < minimumNormalZ) continue;
        const float candidateD = -QVector3D::dotProduct(candidateNormal, vectorFor(modelCandidates[ia]));
        bool containsSeeds = true;
        for (int seed : seedIndices) if (residualFor(candidateNormal, candidateD, points[seed]) > surfaceTolerance) { containsSeeds = false; break; }
        if (!containsSeeds) continue;
        QVector<int> inliers;
        for (int index : modelCandidates) {
            const auto &p = points[index];
            const float value = std::abs(candidateNormal.x() * p.x + candidateNormal.y() * p.y + candidateNormal.z() * p.z + candidateD) / (options.useZAxisResidual ? qMax(std::abs(candidateNormal.z()), 1.0e-6f) : 1.0f);
            if (value <= surfaceTolerance) inliers.push_back(index);
        }
        if (inliers.size() > bestModelInliers.size()) { bestModelInliers = std::move(inliers); bestNormal = candidateNormal; bestD = candidateD; }
    }
    QVector<int> refinedInliers;
    for (int index : result.candidateIndices) {
        const auto &p = points[index];
        const float value = std::abs(bestNormal.x() * p.x + bestNormal.y() * p.y + bestNormal.z() * p.z + bestD) / (options.useZAxisResidual ? qMax(std::abs(bestNormal.z()), 1.0e-6f) : 1.0f);
        if (value <= surfaceTolerance) refinedInliers.push_back(index);
    }
    if (refinedInliers.size() < required) { result.error = QStringLiteral("RANSAC 平面内点不足：%1 / %2").arg(refinedInliers.size()).arg(required); return result; }
    QVector3D finalNormal = bestNormal;
    float finalD = bestD;
    double finalEigenvalues[3] = {};
    const int refinementIterations = qBound(1, options.pcaRefinementIterations, 3);
    for (int iteration = 0; iteration < refinementIterations; ++iteration) {
        double mean[3] = {};
        for (int index : refinedInliers) { mean[0] += points[index].x; mean[1] += points[index].y; mean[2] += points[index].z; }
        for (double &v : mean) v /= refinedInliers.size();
        double cov[3][3] = {};
        for (int index : refinedInliers) {
            const double delta[3] = {points[index].x - mean[0], points[index].y - mean[1], points[index].z - mean[2]};
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) cov[r][c] += delta[r] * delta[c];
        }
        double vectors[3][3] = {};
        symmetricEigen3(cov, finalEigenvalues, vectors);
        finalNormal = QVector3D(float(vectors[0][2]), float(vectors[1][2]), float(vectors[2][2]));
        if (finalNormal.lengthSquared() <= 1.0e-14f) { result.error = QStringLiteral("最小二乘/PCA 平面拟合失败"); return result; }
        finalNormal.normalize();
        if (QVector3D::dotProduct(finalNormal, bestNormal) < 0.0f) finalNormal = -finalNormal;
        if (finalNormal.z() < 0.0f) finalNormal = -finalNormal;
        if (options.useZAxisResidual && finalNormal.z() < minimumNormalZ) { result.error = QStringLiteral("最小二乘/PCA 平面不符合 2.5D 约束"); return result; }
        finalD = -QVector3D::dotProduct(finalNormal, QVector3D(float(mean[0]), float(mean[1]), float(mean[2])));
        QVector<int> reclassified;
        for (int index : result.candidateIndices) if (residualFor(finalNormal, finalD, points[index]) <= surfaceTolerance) reclassified.push_back(index);
        ++result.pcaRefinementCount;
        if (reclassified.size() < required) { result.error = QStringLiteral("PCA 重分类后平面内点不足：%1 / %2").arg(reclassified.size()).arg(required); return result; }
        if (reclassified == refinedInliers) break;
        refinedInliers = std::move(reclassified);
    }
    for (int seed : seedIndices) if (residualFor(finalNormal, finalD, points[seed]) > surfaceTolerance) { result.error = QStringLiteral("最终拟合平面未包含全部控制点"); return result; }
    result.planarity = float(1.0 - finalEigenvalues[2] / qMax(finalEigenvalues[1], 1.0e-20));
    if (options.deferFinalClassification) {
        result.planeIndices = refinedInliers;
        result.deferred = true;
    } else {
        QVector<int> classified;
        for (int i = 0; i < points.size(); ++i) if (finite(points[i]) && residualFor(finalNormal, finalD, points[i]) <= surfaceTolerance) classified.push_back(i);
        const QVector3D originPoint = QVector3D(-finalD * finalNormal.x(), -finalD * finalNormal.y(), -finalD * finalNormal.z());
        const QVector3D axisU = QVector3D::crossProduct(finalNormal, std::abs(finalNormal.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0)).normalized();
        const QVector3D axisV = QVector3D::crossProduct(finalNormal, axisU).normalized();
        QVector<QVector2D> projected; projected.reserve(classified.size());
        std::unordered_map<Cell, QVector<int>, CellHash> grid;
        float minU = std::numeric_limits<float>::max(), minV = minU;
        float maxU = std::numeric_limits<float>::lowest(), maxV = maxU;
        for (int index : classified) {
            const QVector3D delta = vectorFor(index) - originPoint;
            const QVector2D uv(QVector3D::dotProduct(delta, axisU), QVector3D::dotProduct(delta, axisV));
            projected.push_back(uv);
            minU = qMin(minU, uv.x()); maxU = qMax(maxU, uv.x());
            minV = qMin(minV, uv.y()); maxV = qMax(maxV, uv.y());
        }
        const float spacing = float(std::sqrt(double(qMax(maxU - minU, 1.0e-6f)
                                               * qMax(maxV - minV, 1.0e-6f))
                                               / qMax(1, projected.size())));
        const float radius = options.connectivityRadius > 0.0f
            ? options.connectivityRadius : qMax(spacing * 2.5f, 1.0e-6f);
        for (int local = 0; local < projected.size(); ++local)
            grid[{int(std::floor(projected[local].x() / radius)), int(std::floor(projected[local].y() / radius))}].push_back(local);
        QVector<int> component(classified.size(), -1), sizes;
        for (int start = 0, count = 0; start < classified.size(); ++start) if (component[start] < 0) { QVector<int> queue{start}; component[start] = count; for (int head = 0; head < queue.size(); ++head) { const Cell cell{int(std::floor(projected[queue[head]].x() / radius)), int(std::floor(projected[queue[head]].y() / radius))}; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const auto it = grid.find({cell.x + dx, cell.y + dy}); if (it == grid.end()) continue; for (int neighbor : it->second) if (component[neighbor] < 0 && (projected[neighbor] - projected[queue[head]]).lengthSquared() <= radius * radius) { component[neighbor] = count; queue.push_back(neighbor); } } } sizes.push_back(queue.size()); ++count; }
        result.connectedComponentCount = sizes.size();
        int selected = -1;
        if (options.keepSeedComponentOnly) for (int seed : seedIndices) { const int local = classified.indexOf(seed); if (local < 0) { result.error = QStringLiteral("拟合后采样点不在最终平面容差内"); return result; } if (selected < 0) selected = component[local]; else if (selected != component[local]) { result.error = QStringLiteral("采样点不在同一连通区域"); return result; } }
        const int referenceSize = selected >= 0 ? sizes[selected] : (sizes.isEmpty() ? 0 : *std::max_element(sizes.cbegin(), sizes.cend()));
        const int threshold = qMax(qMax(1, options.minimumDisconnectedComponentPoints), int(std::ceil(double(referenceSize) * qBound(0.0f, options.minimumDisconnectedComponentRatio, 1.0f))));
        for (int local = 0; local < classified.size(); ++local) if (!options.keepSeedComponentOnly || component[local] == selected) result.planeIndices.push_back(classified[local]); else if (sizes[component[local]] >= threshold) result.disconnectedPlaneIndices.push_back(classified[local]);
        result.significantComponentCount = 0; for (int size : sizes) if (size >= threshold) ++result.significantComponentCount;
    }
    if (result.planeIndices.size() < required) { result.error = QStringLiteral("目标连通区域点数不足：%1 / %2").arg(result.planeIndices.size()).arg(required); return result; }
    double squared = 0.0; float maximum = 0.0f;
    for (int index : result.planeIndices) { result.planePoints.push_back(points[index]); const float distance = residualFor(finalNormal, finalD, points[index]); squared += double(distance) * distance; maximum = qMax(maximum, distance); }
    result.rmsError = float(std::sqrt(squared / qMax(1, result.planeIndices.size())));
    result.usedThreshold = surfaceTolerance;
    result.model = {finalNormal.x(), finalNormal.y(), finalNormal.z(), finalD, int(result.planeIndices.size()), result.rmsError, maximum};
    result.ok = true;
    return result;
}

ConsistencyResult validateConsistency(const QVector<pointcloud::Point3D> &points,
                                       const PlaneModel &referencePlane,
                                       const QVector<int> &referenceIndices,
                                       const QVector<int> &verificationIndices,
                                       float angleToleranceDegrees,
                                       float distanceToleranceMm)
{
    ConsistencyResult result;
    const auto fail = [&result](ConsistencyStatus status, const QString &message) {
        result.status = status; result.error = message; return result;
    };
    if (referenceIndices.size() < 3 || verificationIndices.size() != 3
        || !std::isfinite(angleToleranceDegrees) || angleToleranceDegrees < 0.0f
        || !std::isfinite(distanceToleranceMm) || distanceToleranceMm < 0.0f)
        return fail(ConsistencyStatus::InvalidInput, QStringLiteral("平面校验输入或阈值无效"));
    const auto valid = [&points](int i) { return i >= 0 && i < points.size(); };
    const auto finite = [](const pointcloud::Point3D &p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    for (int i = 0; i < referenceIndices.size(); ++i) {
        if (!valid(referenceIndices[i]) || !finite(points[referenceIndices[i]])) return fail(ConsistencyStatus::InvalidInput, QStringLiteral("平面校验点索引越界或包含无效点"));
        for (int j = i + 1; j < referenceIndices.size(); ++j) if (referenceIndices[i] == referenceIndices[j]) return fail(ConsistencyStatus::ReusedPoint, QStringLiteral("第一组采样点必须使用不同的点"));
    }
    for (int i = 0; i < 3; ++i) {
        const int index = verificationIndices[i];
        if (!valid(index) || !finite(points[index])) return fail(ConsistencyStatus::InvalidInput, QStringLiteral("平面校验点索引越界或包含无效点"));
        for (int j = i + 1; j < 3; ++j) if (verificationIndices[i] == verificationIndices[j]) return fail(ConsistencyStatus::ReusedPoint, QStringLiteral("第二组三点必须使用不同的点"));
        if (referenceIndices.contains(index)) return fail(ConsistencyStatus::ReusedPoint, QStringLiteral("第二组三点不能复用第一组三点"));
    }
    QVector3D referenceNormal(referencePlane.a, referencePlane.b, referencePlane.c);
    if (!std::isfinite(referencePlane.a) || !std::isfinite(referencePlane.b) || !std::isfinite(referencePlane.c) || !std::isfinite(referencePlane.d) || referenceNormal.lengthSquared() <= 1.0e-10f) return fail(ConsistencyStatus::InvalidInput, QStringLiteral("第一拟合平面无效"));
    const QVector3D p1(points[verificationIndices[0]].x, points[verificationIndices[0]].y, points[verificationIndices[0]].z);
    const QVector3D p2(points[verificationIndices[1]].x, points[verificationIndices[1]].y, points[verificationIndices[1]].z);
    const QVector3D p3(points[verificationIndices[2]].x, points[verificationIndices[2]].y, points[verificationIndices[2]].z);
    QVector3D verificationNormal = QVector3D::crossProduct(p2 - p1, p3 - p1);
    if (verificationNormal.lengthSquared() <= 1.0e-10f) return fail(ConsistencyStatus::Collinear, QStringLiteral("第二组三点近似共线，无法确定平面"));
    const float norm = referenceNormal.length(); const float normalizedD = referencePlane.d / norm; referenceNormal /= norm; verificationNormal.normalize();
    result.normalAngleDegrees = qRadiansToDegrees(std::acos(qBound(-1.0f, std::abs(QVector3D::dotProduct(referenceNormal, verificationNormal)), 1.0f)));
    const auto distance = [&referenceNormal, normalizedD](const QVector3D &p) { return std::abs(QVector3D::dotProduct(referenceNormal, p) + normalizedD); };
    result.maximumDistanceMm = qMax(qMax(distance(p1), distance(p2)), distance(p3));
    if (result.normalAngleDegrees > angleToleranceDegrees) return fail(ConsistencyStatus::AngleExceeded, QStringLiteral("第二组三点平面夹角超过阈值"));
    if (result.maximumDistanceMm > distanceToleranceMm) return fail(ConsistencyStatus::DistanceExceeded, QStringLiteral("第二组三点到第一拟合平面的距离超过阈值"));
    result.status = ConsistencyStatus::Passed; result.passed = true; return result;
}

BoundsCenterResult calculateBoundsCenter(const QVector<pointcloud::Point3D> &points,
                                         const QVector<int> &indices)
{
    BoundsCenterResult result;
    if (indices.isEmpty()) { result.error = QStringLiteral("最终平面点为空"); return result; }
    QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D maximum(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (int index : indices) {
        if (index < 0 || index >= points.size() || !std::isfinite(points[index].x) || !std::isfinite(points[index].y) || !std::isfinite(points[index].z)) { result.error = QStringLiteral("最终平面索引越界或包含无效点"); return result; }
        const auto &p = points[index]; minimum.setX(qMin(minimum.x(), p.x)); minimum.setY(qMin(minimum.y(), p.y)); minimum.setZ(qMin(minimum.z(), p.z)); maximum.setX(qMax(maximum.x(), p.x)); maximum.setY(qMax(maximum.y(), p.y)); maximum.setZ(qMax(maximum.z(), p.z));
    }
    result.center = (minimum + maximum) * 0.5f; result.ok = true; return result;
}

} // namespace pcv::planefitting
