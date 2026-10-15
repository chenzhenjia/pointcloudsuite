#pragma once

#include <QElapsedTimer>
#include <QVector2D>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

namespace pcv::planefitting::detail {

struct ProjectionCell {
    qint64 u, v;
    bool operator==(const ProjectionCell &b) const { return u == b.u && v == b.v; }
};
struct ProjectionHash {
    size_t operator()(const ProjectionCell &p) const {
        return std::hash<quint64>{}(quint64(p.u) * 73856093u ^ quint64(p.v) * 19349663u);
    }
};

struct ProjectedSelection {
    QVector<int> localIndices;
    int mergedRegions = 0;
    double gapMs = 0;
};

// First label normal sampling components with an anisotropic metric. Then
// connect components by real boundary-point distances, without filling holes.
inline ProjectedSelection selectProjectedComponents(
    const QVector<QVector2D> &projected, int seed, float radiusU, float radiusV,
    float maximumGap, const std::function<bool()> &cancelled)
{
    ProjectedSelection result;
    struct Bucket {
        int pending = -1;
        int original = -1;
        std::array<int, 9> neighbors{-1,-1,-1,-1,-1,-1,-1,-1,-1};
        bool boundary = false;
    };
    std::unordered_map<ProjectionCell, int, ProjectionHash> grid;
    const size_t expectedCells=qMax(size_t(1),size_t(projected.size())/4);
    grid.reserve(expectedCells);
    std::vector<Bucket> buckets; buckets.reserve(expectedCells);
    QVector<int> cells(projected.size()), originalNext(projected.size()), next(projected.size()), previous(projected.size(),-1);
    for (int i = 0; i < projected.size(); ++i) {
        if ((i & 4095) == 0 && cancelled()) return {};
        const auto p = projected.at(i);
        const auto inserted=grid.try_emplace(ProjectionCell{qint64(std::floor(p.x()/radiusU)),qint64(std::floor(p.y()/radiusV))},int(buckets.size()));
        if(inserted.second)buckets.push_back({});
        const int id=inserted.first->second;auto &cell=buckets[size_t(id)];cells[i]=id;
        originalNext[i]=cell.original;next[i]=cell.pending;
        if(cell.pending>=0)previous[cell.pending]=i;
        cell.original=i;cell.pending=i;
    }
    for (auto &entry : grid) {
        if (cancelled()) return {};
        buckets[size_t(entry.second)].neighbors[4]=entry.second;
        // Each undirected neighbor pair needs one hash lookup. Fill both
        // directions together; the exact point-distance checks are unchanged.
        for (int n=5;n<9;++n) {
            const int du=n%3-1,dv=n/3-1;
            const auto it=grid.find({entry.first.u+du,entry.first.v+dv});
            if(it!=grid.end()) {
                buckets[size_t(entry.second)].neighbors[size_t(n)]=it->second;
                buckets[size_t(it->second)].neighbors[size_t(8-n)]=entry.second;
            }
        }
    }
    QVector<int> labels(projected.size(), -1), queue;
    int components = 0;
    // Intrusive lists use contiguous arrays rather than allocating two small
    // Qt buffers per spatial cell. Removing a visited point remains O(1).
    const auto remove = [&buckets,&cells,&next,&previous](int point) {
        const int before=previous.at(point),after=next.at(point);
        if(before>=0)next[before]=after;else buckets[size_t(cells.at(point))].pending=after;
        if(after>=0)previous[after]=before;
    };
    for (int start = 0; start < projected.size(); ++start) {
        if ((start & 1023) == 0 && cancelled()) return {};
        if (labels.at(start) >= 0) continue;
        queue.clear(); queue.push_back(start); labels[start] = components;
        remove(start);
        for (int head = 0; head < queue.size(); ++head) {
            if (cancelled()) return {};
            const int current = queue.at(head); const auto p = projected.at(current);
            for (int bucket : buckets[size_t(cells.at(current))].neighbors) {
                if (bucket<0) continue;
                int inspected=0;
                for (int other=buckets[size_t(bucket)].pending;other>=0;) {
                    if ((++inspected & 1023) == 0 && cancelled()) return {};
                    const int after=next.at(other);const auto delta = projected.at(other) - p;
                    const double du = double(delta.x()) / radiusU, dv = double(delta.y()) / radiusV;
                    if (du * du + dv * dv <= 1.0) {
                        labels[other] = components; queue.push_back(other); remove(other);
                    }
                    other=after;
                }
            }
        }
        ++components;
    }
    QVector<quint8> included(components, 0); included[labels.at(seed)] = 1;
    if (maximumGap > 0 && components > 1) {
        QElapsedTimer timer; timer.start();
        // A bucket is on a component boundary if a neighbor is absent or has
        // another label. Mixed buckets are also boundaries. Keep actual points.
        for (auto &entry : grid) {
            if (cancelled()) return {};
            auto &bucket = buckets[size_t(entry.second)]; const int label = labels.at(bucket.original);
            for (int i=bucket.original;i>=0;i=originalNext.at(i)) if (labels.at(i) != label) bucket.boundary = true;
            for (int neighbor : bucket.neighbors) {
                if (neighbor<0) { bucket.boundary = true; continue; }
                for (int i=buckets[size_t(neighbor)].original;i>=0;i=originalNext.at(i)) if (labels.at(i) != label) { bucket.boundary = true; break; }
            }
        }
        std::vector<QVector<int>> boundaries(static_cast<size_t>(components));
        struct GapBucket { QVector<int> pending; std::array<GapBucket *, 9> neighbors{}; };
        std::unordered_map<ProjectionCell, GapBucket, ProjectionHash> gapGrid;
        std::vector<GapBucket *> gapCells(size_t(projected.size()), nullptr);
        QVector<int> gapPositions(projected.size(), -1);
        for (int i = 0; i < projected.size(); ++i) {
            if ((i & 4095) == 0 && cancelled()) return {};
            if (!buckets[size_t(cells.at(i))].boundary) continue;
            boundaries[size_t(labels.at(i))].push_back(i);
            const auto p = projected.at(i);
            auto &bucket = gapGrid[{qint64(std::floor(p.x() / maximumGap)), qint64(std::floor(p.y() / maximumGap))}];
            gapCells[size_t(i)] = &bucket; gapPositions[i] = int(bucket.pending.size()); bucket.pending.push_back(i);
        }
        for (auto &entry : gapGrid) {
            if (cancelled()) return {};
            int n = 0;
            for (int dv = -1; dv <= 1; ++dv) for (int du = -1; du <= 1; ++du) {
                const auto it = gapGrid.find({entry.first.u + du, entry.first.v + dv});
                entry.second.neighbors[size_t(n++)] = it == gapGrid.end() ? nullptr : &it->second;
            }
        }
        const auto removeRegion = [&](int region) {
            for (int i : boundaries[size_t(region)]) {
                auto &pending = gapCells[size_t(i)]->pending; const int slot = gapPositions.at(i);
                const int moved = pending.last(); pending[slot] = moved;
                gapPositions[moved] = slot; pending.removeLast();
            }
        };
        QVector<int> regions{labels.at(seed)}; removeRegion(regions.first());
        const double gapSquared = double(maximumGap) * maximumGap;
        for (int head = 0; head < regions.size(); ++head) {
            for (int i : boundaries[size_t(regions.at(head))]) {
                if (cancelled()) return {};
                const auto p = projected.at(i);
                for (auto *bucket : gapCells[size_t(i)]->neighbors) {
                    if (!bucket) continue;
                    for (int slot = 0; slot < bucket->pending.size();) {
                        if ((slot & 1023) == 0 && cancelled()) return {};
                        const int other = bucket->pending.at(slot); const auto d = projected.at(other) - p;
                        if (double(d.x()) * d.x() + double(d.y()) * d.y() <= gapSquared) {
                            const int region = labels.at(other); included[region] = 1; regions.push_back(region);
                            removeRegion(region); ++result.mergedRegions;
                            // Removal swaps any element into this slot. Restart
                            // to include swapped points from all removed slots.
                            slot = 0;
                        } else ++slot;
                    }
                }
            }
        }
        result.gapMs = timer.nsecsElapsed() / 1e6;
    }
    for (int i = 0; i < projected.size(); ++i) {
        if ((i & 4095) == 0 && cancelled()) return {};
        if (included.at(labels.at(i))) result.localIndices.push_back(i);
    }
    return result;
}
} // namespace pcv::planefitting::detail
