#include <pcv/stitching/seam_fusion.h>
#include <pcv/stitching/seam_fusion_kernel.h>

#include <QElapsedTimer>
#include <QHash>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pointcloud {
namespace {
struct Cell3 { qint64 x = 0, y = 0, z = 0; bool operator==(const Cell3 &o) const { return x == o.x && y == o.y && z == o.z; } };
size_t qHash(const Cell3 &c, size_t seed = 0) noexcept { return qHashMulti(seed, c.x, c.y, c.z); }
QVector3D pv(const Point3D &p) { return {p.x, p.y, p.z}; }
QVector3D tr(const QMatrix4x4 &m) { return {m(0, 3), m(1, 3), m(2, 3)}; }
Cell3 cell3(const QVector3D &p, float s) { return {qint64(std::floor(p.x() / s)), qint64(std::floor(p.y() / s)), qint64(std::floor(p.z() / s))}; }
Point3D blended(const Point3D &a, const Point3D &b, float w) {
    Point3D p; const float v = 1.0f - w;
    p.x = a.x * v + b.x * w; p.y = a.y * v + b.y * w; p.z = a.z * v + b.z * w;
    p.nx = a.nx * v + b.nx * w; p.ny = a.ny * v + b.ny * w; p.nz = a.nz * v + b.nz * w;
    return p;
}
struct Seam { QVector3D normal, travel; float offset = 0.0f; float aMin = 0.0f, aMax = 0.0f, bMin = 0.0f, bMax = 0.0f, overlapMin = 0.0f, overlapMax = 0.0f; bool valid = false; };
}

SeamFusionResult applyTrajectorySeamFusionImpl(MultiFrameRegistrationResult *merge,
                                             const SeamFusionOptions &options)
{
    SeamFusionResult result;
    if (!merge) { result.error = QStringLiteral("接缝输入为空"); return result; }
    result.inputPoints = merge->points.size(); result.outputPoints = result.inputPoints;
    const auto cancelled = [&]() {
        if (cancellationRequested(options)) {
            result.cancelled = true;
            result.error = QStringLiteral("已取消");
            return true;
        }
        return false;
    };
    if (!options.enabled) { result.ok = true; return result; }
    if (cancelled()) return result;
    if (options.halfWidth <= 0.0f || options.mutualDistance <= 0.0f || options.decisionCellSize <= 0.0f) { result.error = QStringLiteral("接缝参数必须为正数"); return result; }
    if (merge->points.size() != merge->cloudIds.size() || merge->points.size() != merge->sourceIndices.size() || merge->points.size() != merge->scanRatios.size()) { result.error = QStringLiteral("接缝输入点与来源映射长度不一致"); return result; }
    if (merge->frameMetadata.size() < 2 || merge->registrationCorrections.size() != merge->frameMetadata.size()) { result.error = QStringLiteral("接缝帧元数据不完整"); return result; }
    for (int id : merge->cloudIds) if (id < 0 || id >= merge->frameMetadata.size()) { result.error = QStringLiteral("接缝点云来源索引无效"); return result; }

    QVector<QVector3D> centers, directions; centers.reserve(merge->frameMetadata.size()); directions.reserve(merge->frameMetadata.size());
    for (int i = 0; i < merge->frameMetadata.size(); ++i) {
        if (cancelled()) return result;
        const auto &frame = merge->frameMetadata[i]; const QMatrix4x4 correction = merge->registrationCorrections[i];
        const QVector3D a = tr(frame.startBaseFromFlange), b = tr(frame.endBaseFromFlange); const QVector3D direction = correction.mapVector(b - a);
        if (direction.lengthSquared() <= 1.0e-12f) { result.error = QStringLiteral("无法确定接缝扫描方向"); return result; }
        centers.push_back(correction.map((a + b) * 0.5f)); directions.push_back(direction.normalized());
    }
    const QVector3D axisVector = centers.last() - centers.first(); if (axisVector.lengthSquared() <= 1.0e-12f) { result.error = QStringLiteral("接缝扫描中心重合"); return result; }
    const QVector3D axis = axisVector.normalized();
    for (int i = 1; i < centers.size(); ++i) if (QVector3D::dotProduct(centers[i] - centers[i - 1], axis) <= 1.0e-6f) { SeamFusionDiagnostic d; d.cloudA = i - 1; d.cloudB = i; d.reason = QStringLiteral("扫描未沿同一拼接方向排序，已拒绝接缝裁剪"); result.diagnostics.push_back(d); result.ok = true; return result; }

    // Per-cloud buckets: one pass over the merged cloud. Each seam then only
    // touches the two neighbouring clouds instead of rescanning every point,
    // and per-seam keep/matched marks stay local to the buckets (O(N) total).
    QVector<QVector<qsizetype>> buckets(centers.size());
    for (qsizetype k = 0; k < merge->points.size(); ++k) {
        if ((k & 0x3fff) == 0 && cancelled()) return result;
        buckets[merge->cloudIds[k]].push_back(k);
    }

    QVector<Seam> seams(centers.size() - 1);
    QVector<QVector<qsizetype>> left(seams.size()), right(seams.size());
    for (int i = 0; i + 1 < centers.size(); ++i) {
        Seam &s = seams[i]; s.normal = (centers[i + 1] - centers[i]).normalized(); s.offset = QVector3D::dotProduct((centers[i] + centers[i + 1]) * 0.5f, s.normal);
        s.travel = directions[i] + directions[i + 1]; s.travel -= s.normal * QVector3D::dotProduct(s.travel, s.normal); if (s.travel.lengthSquared() <= 1.0e-12f) { result.error = QStringLiteral("无法确定接缝处扫描行程方向"); return result; } s.travel.normalize();
        s.aMin = std::numeric_limits<float>::max(); s.aMax = -s.aMin; s.bMin = s.aMin; s.bMax = -s.aMin;
        const QVector<qsizetype> &bucketA = buckets[i];
        const QVector<qsizetype> &bucketB = buckets[i + 1];
        qsizetype visited = 0;
        for (qsizetype local = 0; local < bucketA.size(); ++local) {
            if ((visited++ & 0x3fff) == 0 && cancelled()) return result;
            const float value = QVector3D::dotProduct(pv(merge->points[bucketA[local]]), s.normal);
            s.aMin = qMin(s.aMin, value); s.aMax = qMax(s.aMax, value);
        }
        visited = 0;
        for (qsizetype local = 0; local < bucketB.size(); ++local) {
            if ((visited++ & 0x3fff) == 0 && cancelled()) return result;
            const float value = QVector3D::dotProduct(pv(merge->points[bucketB[local]]), s.normal);
            s.bMin = qMin(s.bMin, value); s.bMax = qMax(s.bMax, value);
        }
        s.overlapMin = qMax(s.aMin, s.bMin); s.overlapMax = qMin(s.aMax, s.bMax); s.valid = s.aMin <= s.aMax && s.bMin <= s.bMax && s.overlapMax > s.overlapMin; if (s.valid) s.offset = (s.overlapMin + s.overlapMax) * 0.5f;
        // Band points of the two neighbouring clouds only (valid seams only,
        // matching the original per-point scan guards).
        if (s.valid) {
            visited = 0;
            for (qsizetype local = 0; local < bucketA.size(); ++local) {
                if ((visited++ & 0x3fff) == 0 && cancelled()) return result;
                const qsizetype k = bucketA[local];
                const QVector3D p = pv(merge->points[k]);
                if (std::abs(QVector3D::dotProduct(p, s.normal) - s.offset) < options.halfWidth)
                    left[i].push_back(k);
            }
            visited = 0;
            for (qsizetype local = 0; local < bucketB.size(); ++local) {
                if ((visited++ & 0x3fff) == 0 && cancelled()) return result;
                const qsizetype k = bucketB[local];
                const QVector3D p = pv(merge->points[k]);
                if (std::abs(QVector3D::dotProduct(p, s.normal) - s.offset) < options.halfWidth)
                    right[i].push_back(k);
            }
        }
    }

    QVector<bool> usable(seams.size(), false);
    for (int i = 0; i < seams.size(); ++i)
        usable[i] = seams[i].valid && !left[i].isEmpty() && !right[i].isEmpty();

    QVector<Point3D> out;
    QVector<int> ids;
    QVector<qsizetype> sources;
    QVector<float> ratios;
    out.reserve(merge->points.size());
    ids.reserve(out.capacity());
    sources.reserve(out.capacity());
    ratios.reserve(out.capacity());
    auto append = [&](const Point3D &p, int id, qsizetype source, float ratio) {
        out.push_back(p);
        ids.push_back(id);
        sources.push_back(source);
        ratios.push_back(ratio);
    };
    QVector<quint8> handled(merge->points.size(), 0);
    // Core pass: a single global scan (the fused arrays keep the input order);
    // keep decisions are evaluated per point from its adjacent usable seams.
    for (qsizetype k = 0; k < merge->points.size(); ++k) {
        if ((k & 0x3fff) == 0 && cancelled()) return result;
        const int id = merge->cloudIds[k];
        const QVector3D p = pv(merge->points[k]);
        bool keepPoint = true;
        if (id > 0 && usable[id - 1])
            keepPoint = keepPoint && QVector3D::dotProduct(p, seams[id - 1].normal) - seams[id - 1].offset >= options.halfWidth;
        if (id + 1 < centers.size() && usable[id])
            keepPoint = keepPoint && QVector3D::dotProduct(p, seams[id].normal) - seams[id].offset <= -options.halfWidth;
        if (keepPoint) {
            append(merge->points[k], merge->cloudIds[k], merge->sourceIndices[k],
                   merge->scanRatios[k]);
            handled[k] = 1;
        }
    }
    for (int si = 0; si < seams.size(); ++si) {
        SeamFusionDiagnostic d; d.cloudA = si; d.cloudB = si + 1; d.projectedAMin = seams[si].aMin; d.projectedAMax = seams[si].aMax; d.projectedBMin = seams[si].bMin; d.projectedBMax = seams[si].bMax; d.actualOverlapMin = seams[si].overlapMin; d.actualOverlapMax = seams[si].overlapMax; d.seamProjection = seams[si].offset; d.actualOverlapValid = seams[si].valid; d.bandPointsA = left[si].size(); d.bandPointsB = right[si].size(); d.bandPoints = d.bandPointsA + d.bandPointsB; d.corePoints = out.size();
        if (!seams[si].valid || left[si].isEmpty() || right[si].isEmpty()) { d.reason = QStringLiteral("seam_outside_actual_overlap：真实投影区无有效双侧重叠，保留完整点云"); result.diagnostics.push_back(d); continue; }
        // Per-seam grid over the right-side band; entries are LOCAL positions so
        // the matched marks stay local to this seam. (A per-cloud index variant
        // was benchmarked and regressed on real data: each cloud serves only
        // one seam in a linear chain, so indexing the whole cloud costs far
        // more than the band-sized grid it replaces.)
        QHash<Cell3, QVector<int>> grid;
        QElapsedTimer indexClock;
        indexClock.restart();
        for (qsizetype local = 0; local < right[si].size(); ++local)
            grid[cell3(pv(merge->points[right[si][local]]), options.mutualDistance)].push_back(int(local));
        result.seamIndexBuildMs += indexClock.elapsed();
        const float limit2 = options.mutualDistance * options.mutualDistance;
        qsizetype pairs = 0;
        QVector<bool> leftMatched(left[si].size(), false);
        QVector<bool> rightMatched(right[si].size(), false);
        for (qsizetype sourceLocal = 0; sourceLocal < left[si].size(); ++sourceLocal) {
            if ((pairs & 0x3fff) == 0 && cancelled()) return result;
            const qsizetype source = left[si][sourceLocal];
            const QVector3D p = pv(merge->points[source]);
            const Cell3 c = cell3(p, options.mutualDistance);
            float best = limit2;
            int selectedLocal = -1;
            for (qint64 z = -1; z <= 1; ++z)
                for (qint64 y = -1; y <= 1; ++y)
                    for (qint64 x = -1; x <= 1; ++x) {
                        const auto it = grid.constFind({c.x + x, c.y + y, c.z + z});
                        if (it == grid.cend()) continue;
                        for (int candidateLocal : it.value()) {
                            const qsizetype candidate = right[si][qsizetype(candidateLocal)];
                            const float distance = (pv(merge->points[candidate]) - p).lengthSquared();
                            if (distance <= best) {
                                best = distance;
                                selectedLocal = candidateLocal;
                            }
                        }
                    }
            if (selectedLocal < 0) continue;
            const qsizetype selected = right[si][qsizetype(selectedLocal)];
            const QVector3D midpoint = (p + pv(merge->points[selected])) * 0.5f;
            const float sd = QVector3D::dotProduct(midpoint, seams[si].normal) - seams[si].offset;
            const float w = qBound(0.0f,
                                   (sd + options.halfWidth) / (2.0f * options.halfWidth),
                                   1.0f);
            const int chosen = w >= 0.5f ? int(selected) : int(source);
            append(blended(merge->points[source], merge->points[selected], w),
                   merge->cloudIds[chosen], merge->sourceIndices[chosen],
                   merge->scanRatios[chosen]);
            leftMatched[sourceLocal] = true;
            rightMatched[qsizetype(selectedLocal)] = true;
            handled[source] = 1;
            handled[selected] = 1;
            ++pairs;
            ++d.interpolatedPoints;
        }
        for (qsizetype local = 0; local < left[si].size(); ++local) {
            const qsizetype index = left[si][local];
            if (!leftMatched[local] && !handled[index]) {
                append(merge->points[index], merge->cloudIds[index], merge->sourceIndices[index],
                       merge->scanRatios[index]);
                handled[index] = 1;
                ++d.unmatchedPreserved;
            }
        }
        for (qsizetype local = 0; local < right[si].size(); ++local) {
            const qsizetype index = right[si][local];
            if (!rightMatched[local] && !handled[index]) {
                append(merge->points[index], merge->cloudIds[index], merge->sourceIndices[index],
                       merge->scanRatios[index]);
                handled[index] = 1;
                ++d.unmatchedPreserved;
            }
        }
        d.mutualPairs = pairs;
        d.applied = pairs > 0;
        d.reason = d.applied ? QStringLiteral("参考羽化接缝：候选最近邻插值")
                             : QStringLiteral("融合带无满足距离的对应点，保留核心点和未匹配点");
        result.diagnostics.push_back(d);
    }
    merge->points = std::move(out); merge->cloudIds = std::move(ids); merge->sourceIndices = std::move(sources); merge->scanRatios = std::move(ratios); result.outputPoints = merge->points.size(); result.ok = true; return result;
}

} // namespace pointcloud

namespace pointcloud {

SeamFusionResult applyTrajectorySeamFusion(MultiFrameRegistrationResult *merge,
                                           const SeamFusionOptions &options)
{
    QElapsedTimer wall;
    wall.start();
    SeamFusionResult result = applyTrajectorySeamFusionImpl(merge, options);
    result.seamFusionMs = wall.elapsed();
    return result;
}

SeamFusionResult applyTrajectorySeamFusionKernel(
    QVector<Point3D> *points, QVector<int> *cloudIds,
    QVector<qsizetype> *sourceIndices, QVector<float> *scanRatios,
    const QVector<QMatrix4x4> &frameStarts, const QVector<QMatrix4x4> &frameEnds,
    const QVector<QMatrix4x4> &registrationCorrections,
    const SeamFusionOptions &options)
{
    MultiFrameRegistrationResult merge;
    if (points) merge.points = *points;
    if (cloudIds) merge.cloudIds = *cloudIds;
    if (sourceIndices) merge.sourceIndices = *sourceIndices;
    if (scanRatios) merge.scanRatios = *scanRatios;
    merge.registrationCorrections = registrationCorrections;
    const int frameCount = qMin(frameStarts.size(), frameEnds.size());
    merge.frameMetadata.resize(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        merge.frameMetadata[i].startBaseFromFlange = frameStarts[i];
        merge.frameMetadata[i].endBaseFromFlange = frameEnds[i];
    }
    const SeamFusionResult result = applyTrajectorySeamFusion(&merge, options);
    if (result.ok || result.cancelled) {
        if (points) *points = std::move(merge.points);
        if (cloudIds) *cloudIds = std::move(merge.cloudIds);
        if (sourceIndices) *sourceIndices = std::move(merge.sourceIndices);
        if (scanRatios) *scanRatios = std::move(merge.scanRatios);
    }
    return result;
}

} // namespace pointcloud
