#include <pcv/stitching/heightmap_registration.h>

#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace pointcloud {
namespace {

// Sampling budget of the automatic grid-resolution estimate.
constexpr int kMaximumResolutionSamples = 4000;
constexpr int kMinimumResolutionSamples = 50;
// Huber transition of the robust height residual. Every valid cell remains in
// the score, including the sparse step/hole edges that disambiguate an
// otherwise flat surface, while large isolated elevation errors are capped to
// linear influence.
constexpr double kScoreHuberDeltaMm = 1.0;
// Relative separation between the best and the second best candidate below
// which the search reports heightmap_candidate_ambiguous.
constexpr double kAmbiguitySeparation = 0.02;
// Robust trend trimming.
constexpr double kTrendTrimSigma = 2.5;
constexpr int kTrendIterations = 4;
// Upper bound of cells a single score visits, so one candidate stays O(N)
// with a bounded N even on a very fine grid.
constexpr int kMaximumScoringCells = 40000;

struct GridKey3 {
    qint64 x = 0;
    qint64 y = 0;
    qint64 z = 0;
    bool operator==(const GridKey3 &other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridHash3 {
    size_t operator()(const GridKey3 &key) const noexcept {
        return size_t(quint64(key.x) * 73856093ULL
            ^ quint64(key.y) * 19349663ULL
            ^ quint64(key.z) * 83492791ULL);
    }
};

bool finitePoint(const Point3D &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

float medianOf(QVector<float> *values)
{
    if (!values || values->isEmpty()) return std::numeric_limits<float>::quiet_NaN();
    const qsizetype count = values->size();
    const qsizetype middle = count / 2;
    std::nth_element(values->begin(), values->begin() + middle, values->end());
    const float upper = values->at(middle);
    if ((count % 2) != 0) return upper;
    std::nth_element(values->begin(), values->begin() + middle - 1, values->begin() + middle);
    return 0.5f * (upper + values->at(middle - 1));
}

// A cell is usable for scoring only when its whole (2*radius+1)^2 neighbourhood
// is valid. The outline of the crop is therefore never used as a feature: a
// flat surface stays flat under every candidate instead of scoring better when
// its crop boundary happens to line up, which is what made a featureless plane
// look registrable.
bool neighbourhoodValid(const HeightMap &map, int column, int row, int radius)
{
    for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
        const int currentRow = row + offsetY;
        if (currentRow < 0 || currentRow >= map.height) return false;
        for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
            const int currentColumn = column + offsetX;
            if (currentColumn < 0 || currentColumn >= map.width) return false;
            if (map.mask.at(map.index(currentColumn, currentRow)) == 0) return false;
        }
    }
    return true;
}

// Bilinear read of one per-cell channel. Only neighbours with a non-zero
// interpolation weight are required to be valid, so an exact cell-centre
// position reproduces the stored value and an exact cell-edge position
// averages the two adjacent cells. Missing cells never contribute a value.
// A positive radius additionally requires the surrounding neighbourhood to be
// fully observed on this side.
bool sampleChannel(const HeightMap &map, const QVector<float> &channel,
                   float x, float y, float *value, int radius = 0)
{
    if (!value || map.width <= 0 || map.height <= 0) return false;
    const double resolution = double(map.gridResolutionMm);
    if (!(resolution > 0.0)) return false;
    const double columnPosition = (double(x) - double(map.originX)) / resolution;
    const double rowPosition = (double(y) - double(map.originY)) / resolution;
    if (!std::isfinite(columnPosition) || !std::isfinite(rowPosition)) return false;
    const double columnBase = std::floor(columnPosition);
    const double rowBase = std::floor(rowPosition);
    const double fractionX = columnPosition - columnBase;
    const double fractionY = rowPosition - rowBase;
    const int column = int(columnBase);
    const int row = int(rowBase);
    if (radius > 0 && !neighbourhoodValid(map, column, row, radius)) return false;
    const double weightX[2]{1.0 - fractionX, fractionX};
    const double weightY[2]{1.0 - fractionY, fractionY};
    double accumulated = 0.0;
    double weightSum = 0.0;
    for (int offsetY = 0; offsetY < 2; ++offsetY) {
        const double weightRow = weightY[offsetY];
        if (weightRow <= 1.0e-9) continue;
        const int currentRow = row + offsetY;
        if (currentRow < 0 || currentRow >= map.height) return false;
        for (int offsetX = 0; offsetX < 2; ++offsetX) {
            const double weightColumn = weightX[offsetX];
            if (weightColumn <= 1.0e-9) continue;
            const int currentColumn = column + offsetX;
            if (currentColumn < 0 || currentColumn >= map.width) return false;
            const int cell = map.index(currentColumn, currentRow);
            if (map.mask.at(cell) == 0) return false;
            const double weight = weightColumn * weightRow;
            accumulated += weight * double(channel.at(cell));
            weightSum += weight;
        }
    }
    if (weightSum <= 1.0e-9) return false;
    const double sample = accumulated / weightSum;
    if (!std::isfinite(sample)) return false;
    *value = float(sample);
    return true;
}

bool solve3(double matrix[3][3], double rhs[3], double result[3])
{
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row)
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        if (std::abs(matrix[pivot][column]) < 1.0e-12) return false;
        if (pivot != column) {
            for (int element = 0; element < 3; ++element) std::swap(matrix[column][element], matrix[pivot][element]);
            std::swap(rhs[column], rhs[pivot]);
        }
        const double scale = matrix[column][column];
        for (int element = column; element < 3; ++element) matrix[column][element] /= scale;
        rhs[column] /= scale;
        for (int row = 0; row < 3; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (int element = column; element < 3; ++element) matrix[row][element] -= factor * matrix[column][element];
            rhs[row] -= factor * rhs[column];
        }
    }
    for (int element = 0; element < 3; ++element) result[element] = rhs[element];
    return true;
}

struct TrendPlane {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    int iterations = 0;
    bool ok = false;
};

// Robust plane fit of the valid cells: ordinary least squares followed by
// MAD-sigma trimming, repeated a fixed number of times. The trimming keeps a
// real step, hole or edge from tilting the trend that the score removes, while
// a genuine global slope is still fully removed.
TrendPlane fitRobustTrend(const HeightMap &map)
{
    TrendPlane plane;
    QVector<double> xs;
    QVector<double> ys;
    QVector<double> zs;
    xs.reserve(map.validCellCount);
    for (int row = 0; row < map.height; ++row) {
        for (int column = 0; column < map.width; ++column) {
            const int cell = map.index(column, row);
            if (map.mask.at(cell) == 0) continue;
            xs.push_back(double(map.cellCenterX(column)));
            ys.push_back(double(map.cellCenterY(row)));
            zs.push_back(double(map.elevation.at(cell)));
        }
    }
    const qsizetype count = xs.size();
    if (count < 9) return plane;
    QVector<quint8> keep(count, 1);
    for (int iteration = 0; iteration < kTrendIterations; ++iteration) {
        double sxx = 0.0, sxy = 0.0, syy = 0.0, sx = 0.0, sy = 0.0, sz = 0.0;
        double sxz = 0.0, syz = 0.0, weight = 0.0;
        for (qsizetype index = 0; index < count; ++index) {
            if (keep.at(index) == 0) continue;
            const double x = xs.at(index);
            const double y = ys.at(index);
            const double z = zs.at(index);
            sxx += x * x; sxy += x * y; syy += y * y;
            sx += x; sy += y; sz += z;
            sxz += x * z; syz += y * z;
            weight += 1.0;
        }
        if (weight < 9.0) break;
        double matrix[3][3]{{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, weight}};
        double rhs[3]{sxz, syz, sz};
        double solution[3]{0.0, 0.0, 0.0};
        if (!solve3(matrix, rhs, solution)) break;
        if (!std::isfinite(solution[0]) || !std::isfinite(solution[1]) || !std::isfinite(solution[2])) break;
        plane.a = solution[0];
        plane.b = solution[1];
        plane.c = solution[2];
        plane.iterations = iteration + 1;
        QVector<double> residual(count, 0.0);
        QVector<float> keptResiduals;
        keptResiduals.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            residual[index] = zs.at(index)
                - (plane.a * xs.at(index) + plane.b * ys.at(index) + plane.c);
            if (keep.at(index) != 0) keptResiduals.push_back(float(residual.at(index)));
        }
        const float center = medianOf(&keptResiduals);
        if (!std::isfinite(center)) break;
        for (float &value : keptResiduals) value = std::abs(value - center);
        const float deviation = medianOf(&keptResiduals);
        if (!std::isfinite(deviation)) break;
        const double sigma = qMax(1.0e-4, 1.4826 * double(deviation));
        QVector<quint8> next(count, 0);
        qsizetype kept = 0;
        for (qsizetype index = 0; index < count; ++index) {
            if (std::abs(residual.at(index) - double(center)) <= kTrendTrimSigma * sigma) {
                next[index] = 1;
                ++kept;
            }
        }
        // A collapsing trim would fit the plane to a handful of cells; keep
        // the previous support instead of degrading the trend.
        if (kept < qMax<qsizetype>(9, count / 10)) break;
        const bool stable = next == keep;
        keep = next;
        if (stable) break;
    }
    plane.ok = std::isfinite(plane.a) && std::isfinite(plane.b) && std::isfinite(plane.c);
    return plane;
}

// Stage-I/II scoring. The candidate moves the source by
//   p' = Rz(yaw, centre) * p + (dx, dy, dz)
// and the score is measured on the target grid over the intersection of the
// robot-initial overlap box and the candidate overlap box.
struct ScoreResult {
    bool valid = false;
    double score = std::numeric_limits<double>::max();
    double correlation = 0.0;
    int regionCells = 0;
    int sourceValid = 0;
    int targetValid = 0;
};

} // namespace

bool HeightMap::sampleDetrended(float x, float y, float *value) const
{
    return sampleChannel(*this, detrended, x, y, value);
}

// ---------------------------------------------------------------------------
// Grid resolution
// ---------------------------------------------------------------------------

bool estimateHeightMapGridResolution(const QVector<Point3D> &points,
                                     const HeightMapOptions &options,
                                     float *resolutionMm,
                                     QString *error)
{
    if (resolutionMm) *resolutionMm = 0.0f;
    if (error) error->clear();
    const auto reject = [error](const QString &code) {
        if (error) *error = code;
        return false;
    };
    qsizetype finite = 0;
    QVector3D minimum(std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
    QVector3D maximum(std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest());
    for (const Point3D &point : points) {
        if (!finitePoint(point)) continue;
        ++finite;
        minimum.setX(qMin(minimum.x(), point.x));
        minimum.setY(qMin(minimum.y(), point.y));
        minimum.setZ(qMin(minimum.z(), point.z));
        maximum.setX(qMax(maximum.x(), point.x));
        maximum.setY(qMax(maximum.y(), point.y));
        maximum.setZ(qMax(maximum.z(), point.z));
    }
    if (finite < qMax(1, options.minimumValidPoints))
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    const double spanX = double(maximum.x()) - double(minimum.x());
    const double spanY = double(maximum.y()) - double(minimum.y());
    const double spanZ = double(maximum.z()) - double(minimum.z());
    if (!(spanX >= 0.0) || !(spanY >= 0.0) || !(spanZ >= 0.0))
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    // First guess: the X/Y footprint of the cloud divided by its point count is
    // the average per-point spacing of a height field, which is exactly the
    // scale the neighbour index needs. A flat cloud has almost no Z extent, so
    // the volume is not a usable proxy here.
    double cell = std::sqrt(qMax(1.0e-6, spanX * spanY) / double(finite));
    if (!std::isfinite(cell) || cell <= 0.0)
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    const qsizetype stride = qMax<qsizetype>(1, finite / kMaximumResolutionSamples);
    QVector<QVector3D> samples;
    samples.reserve(qMin<qsizetype>(finite, kMaximumResolutionSamples + 1));
    qsizetype seen = 0;
    for (const Point3D &point : points) {
        if (!finitePoint(point)) continue;
        if ((seen++ % stride) != 0) continue;
        samples.push_back(QVector3D(point.x, point.y, point.z));
    }
    if (samples.size() < kMinimumResolutionSamples)
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    std::unordered_map<GridKey3, QVector<int>, GridHash3> cells;
    cells.reserve(size_t(samples.size()));
    // The index is retried with a coarser cell when the first guess cannot reach
    // a neighbour: the estimate is only declared unstable when no scale finds
    // one, which keeps a sparse or oddly distributed cloud registrable.
    const auto keyOf = [&cell](const QVector3D &point) {
        return GridKey3{qint64(std::floor(double(point.x()) / cell)),
                        qint64(std::floor(double(point.y()) / cell)),
                        qint64(std::floor(double(point.z()) / cell))};
    };
    QVector<float> distances;
    distances.reserve(samples.size());
    for (int attempt = 0; attempt < 4; ++attempt) {
        cells.clear();
        cells.reserve(size_t(samples.size()));
        for (int index = 0; index < samples.size(); ++index)
            cells[keyOf(samples.at(index))].push_back(index);
        distances.clear();
        for (int index = 0; index < samples.size(); ++index) {
            const QVector3D &center = samples.at(index);
            const GridKey3 key = keyOf(center);
            float best = std::numeric_limits<float>::max();
            for (int radius = 1; radius <= 2 && best == std::numeric_limits<float>::max(); ++radius) {
                for (qint64 z = -radius; z <= radius; ++z) {
                    for (qint64 y = -radius; y <= radius; ++y) {
                        for (qint64 x = -radius; x <= radius; ++x) {
                            const auto found = cells.find(GridKey3{key.x + x, key.y + y, key.z + z});
                            if (found == cells.end()) continue;
                            for (int candidate : found->second) {
                                if (candidate == index) continue;
                                const float distance = (samples.at(candidate) - center).lengthSquared();
                                if (distance < best) best = distance;
                            }
                        }
                    }
                }
            }
            if (best < std::numeric_limits<float>::max()) distances.push_back(std::sqrt(best));
        }
        if (distances.size() >= kMinimumResolutionSamples
            && double(distances.size()) >= 0.5 * double(samples.size())) break;
        cell *= 4.0;
        if (!std::isfinite(cell)) break;
    }
    if (distances.size() < kMinimumResolutionSamples
        || double(distances.size()) < 0.5 * double(samples.size()))
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    const float median = medianOf(&distances);
    if (!std::isfinite(median) || median <= 0.0f)
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));
    const float minimumGrid = qMin(options.minimumGridResolutionMm, options.maximumGridResolutionMm);
    const float maximumGrid = qMax(options.minimumGridResolutionMm, options.maximumGridResolutionMm);
    if (resolutionMm) *resolutionMm = qBound(minimumGrid, median, maximumGrid);
    return true;
}

// ---------------------------------------------------------------------------
// Height map construction
// ---------------------------------------------------------------------------

HeightMap buildHeightMap(const QVector<Point3D> &points,
                         const HeightMapRegion &region,
                         float gridResolutionMm,
                         const HeightMapOptions &options)
{
    HeightMap map;
    if (!finitePoint(Point3D{region.minimumX, region.minimumY, 0.0f})
        || !region.valid() || !(gridResolutionMm > 0.0f)
        || !std::isfinite(gridResolutionMm)) {
        map.error = QStringLiteral("heightmap_region_invalid");
        return map;
    }
    const double resolution = double(gridResolutionMm);
    const double margin = double(options.overlapMarginMm);
    const double lowX = double(region.minimumX) - margin;
    const double lowY = double(region.minimumY) - margin;
    const double highX = double(region.maximumX) + margin;
    const double highY = double(region.maximumY) + margin;
    const double columns = std::ceil((highX - lowX) / resolution);
    const double rows = std::ceil((highY - lowY) / resolution);
    if (!(columns >= 1.0) || !(rows >= 1.0) || columns * rows > 4.0e7) {
        map.error = QStringLiteral("heightmap_region_invalid");
        return map;
    }
    map.gridResolutionMm = gridResolutionMm;
    map.width = int(columns);
    map.height = int(rows);
    map.originX = float(lowX + 0.5 * resolution);
    map.originY = float(lowY + 0.5 * resolution);
    map.localMinimum = QVector3D(float(lowX), float(lowY), std::numeric_limits<float>::max());
    map.localMaximum = QVector3D(float(highX), float(highY), std::numeric_limits<float>::lowest());
    map.cloudMinimum = QVector3D(std::numeric_limits<float>::max(),
                                 std::numeric_limits<float>::max(),
                                 std::numeric_limits<float>::max());
    map.cloudMaximum = QVector3D(std::numeric_limits<float>::lowest(),
                                 std::numeric_limits<float>::lowest(),
                                 std::numeric_limits<float>::lowest());
    const qsizetype cellCount = qsizetype(map.width) * qsizetype(map.height);
    map.elevation.fill(0.0f, cellCount);
    map.detrended.fill(0.0f, cellCount);
    map.mask.fill(0, cellCount);
    map.counts.fill(0, cellCount);
    map.sourceIndices.fill(-1, cellCount);
    QVector<double> sums(cellCount, 0.0);
    for (qsizetype index = 0; index < points.size(); ++index) {
        const Point3D &point = points.at(index);
        // Every non-finite value is rejected before it can reach a cell.
        if (!finitePoint(point)) continue;
        map.cloudMinimum.setX(qMin(map.cloudMinimum.x(), point.x));
        map.cloudMinimum.setY(qMin(map.cloudMinimum.y(), point.y));
        map.cloudMinimum.setZ(qMin(map.cloudMinimum.z(), point.z));
        map.cloudMaximum.setX(qMax(map.cloudMaximum.x(), point.x));
        map.cloudMaximum.setY(qMax(map.cloudMaximum.y(), point.y));
        map.cloudMaximum.setZ(qMax(map.cloudMaximum.z(), point.z));
        const double x = double(point.x);
        const double y = double(point.y);
        if (x < lowX || x > highX || y < lowY || y > highY) continue;
        const int column = int(std::floor((x - lowX) / resolution));
        const int row = int(std::floor((y - lowY) / resolution));
        if (column < 0 || row < 0 || column >= map.width || row >= map.height) continue;
        const int cell = map.index(column, row);
        sums[cell] += double(point.z);
        map.counts[cell] += 1;
        if (map.sourceIndices[cell] < 0) map.sourceIndices[cell] = index;
        if (map.mask[cell] == 0) {
            map.mask[cell] = 1;
            map.localMinimum.setZ(qMin(map.localMinimum.z(), point.z));
            map.localMaximum.setZ(qMax(map.localMaximum.z(), point.z));
        } else {
            map.localMinimum.setZ(qMin(map.localMinimum.z(), point.z));
            map.localMaximum.setZ(qMax(map.localMaximum.z(), point.z));
        }
    }
    if (map.cloudMinimum.x() > map.cloudMaximum.x()) {
        map.error = QStringLiteral("heightmap_insufficient_data");
        return map;
    }
    for (qsizetype cell = 0; cell < cellCount; ++cell) {
        if (map.mask.at(cell) == 0) continue;
        const double value = sums.at(cell) / double(map.counts.at(cell));
        if (!std::isfinite(value)) {
            map.mask[cell] = 0;
            map.counts[cell] = 0;
            continue;
        }
        map.elevation[cell] = float(value);
        ++map.validCellCount;
        map.validPointCount += qsizetype(map.counts.at(cell));
    }
    if (map.validCellCount < qMax(1, options.minimumValidPoints)) {
        map.error = QStringLiteral("heightmap_insufficient_data");
        return map;
    }
    if (double(map.validCellCount) / double(cellCount) < double(options.minimumValidRatio)) {
        map.error = QStringLiteral("heightmap_insufficient_data");
        return map;
    }
    // Robust de-trending. The raw elevation stays in elevation(); only the
    // copy in detrended() loses the global slope, so a tilt that dominates the
    // absolute height cannot dominate the score, and no irreversible smoothing
    // is applied to the source cloud.
    const TrendPlane trend = fitRobustTrend(map);
    map.trendA = trend.a;
    map.trendB = trend.b;
    map.trendC = trend.c;
    map.trendIterations = trend.iterations;
    for (int row = 0; row < map.height; ++row) {
        for (int column = 0; column < map.width; ++column) {
            const int cell = map.index(column, row);
            if (map.mask.at(cell) == 0) continue;
            const double plane = map.trendA * double(map.cellCenterX(column))
                + map.trendB * double(map.cellCenterY(row)) + map.trendC;
            const double value = double(map.elevation.at(cell)) - plane;
            map.detrended[cell] = std::isfinite(value) ? float(value) : 0.0f;
        }
    }
    // A steep face is not a height field: its Z extent is of the same order as
    // its X/Y extent, so the projection would merge physically different
    // surface into one cell. Those pairs fall through to the legacy ICP.
    const double zRange = double(map.localMaximum.z()) - double(map.localMinimum.z());
    const double xyExtent = qMax(highX - lowX, highY - lowY);
    if (!std::isfinite(zRange) || !std::isfinite(xyExtent) || xyExtent <= 0.0) {
        map.error = QStringLiteral("heightmap_insufficient_data");
        return map;
    }
    if (zRange > 0.5 * xyExtent) {
        map.error = QStringLiteral("heightmap_not_heightfield");
        return map;
    }
    map.ok = true;
    return map;
}

// ---------------------------------------------------------------------------
// Trajectory diagnostic
// ---------------------------------------------------------------------------

namespace {

float signOf(float value)
{
    if (!std::isfinite(value)) return 0.0f;
    if (value > 1.0e-6f) return 1.0f;
    if (value < -1.0e-6f) return -1.0f;
    return 0.0f;
}

int dominantAxisOf(const QMatrix4x4 &start, const QMatrix4x4 &end)
{
    const QVector3D delta(end(0, 3) - start(0, 3), end(1, 3) - start(1, 3),
                          end(2, 3) - start(2, 3));
    int axis = -1;
    float best = 0.0f;
    for (int index = 0; index < 3; ++index) {
        const float magnitude = std::abs(delta[index]);
        if (magnitude > best) { best = magnitude; axis = index; }
    }
    return best > 1.0e-4f ? axis : -1;
}

float travelSignOf(const HeightMapTrajectoryInput &frame, int axis)
{
    if (axis >= 0) {
        const float signedTravel = signOf(frame.signedTravel);
        if (signedTravel != 0.0f) return signedTravel;
        const QVector3D delta(frame.endBaseFromFlange(0, 3) - frame.startBaseFromFlange(0, 3),
                              frame.endBaseFromFlange(1, 3) - frame.startBaseFromFlange(1, 3),
                              frame.endBaseFromFlange(2, 3) - frame.startBaseFromFlange(2, 3));
        return signOf(delta[axis]);
    }
    return 0.0f;
}

// Direction of the scanner order along the frame: +1 when scanRatios grows
// with the point order, -1 when it falls, 0 when the order carries no trend.
float scanRatioDirectionOf(const QVector<float> &scanRatios)
{
    if (scanRatios.size() < 2) return 0.0f;
    double sum = 0.0;
    for (qsizetype index = 1; index < scanRatios.size(); ++index) {
        const double previous = double(scanRatios.at(index - 1));
        const double current = double(scanRatios.at(index));
        if (!std::isfinite(previous) || !std::isfinite(current)) return 0.0f;
        sum += current - previous;
    }
    return signOf(float(sum));
}

} // namespace

HeightMapTrajectoryDiagnostic diagnoseHeightMapTrajectory(const HeightMapTrajectoryInput &target,
                                                          const HeightMapTrajectoryInput &source)
{
    HeightMapTrajectoryDiagnostic diagnostic;
    if (!source.scanRatios || !source.points || source.scanRatios->isEmpty()
        || source.scanRatios->size() != source.points->size()) {
        diagnostic.reason = QStringLiteral("trajectory_scan_ratio_missing");
        return diagnostic;
    }
    if (!target.scanRatios || !target.points || target.scanRatios->isEmpty()
        || target.scanRatios->size() != target.points->size()) {
        diagnostic.reason = QStringLiteral("trajectory_scan_ratio_missing");
        return diagnostic;
    }
    // 1. scanRatios inside the valid range.
    for (const QVector<float> *ratios : {source.scanRatios, target.scanRatios}) {
        for (float value : *ratios) {
            if (!std::isfinite(value) || value < -0.01f || value > 1.01f) {
                diagnostic.reason = QStringLiteral("trajectory_scan_ratio_out_of_range");
                return diagnostic;
            }
        }
    }
    diagnostic.scanRatiosInRange = true;
    // 2. Point order against the scanRatios sequence plus the jump detector.
    const QVector<float> &ratios = *source.scanRatios;
    qsizetype forward = 0, backward = 0, jumps = 0, steps = 0;
    for (qsizetype index = 1; index < ratios.size(); ++index) {
        const double delta = double(ratios.at(index)) - double(ratios.at(index - 1));
        if (!std::isfinite(delta)) {
            diagnostic.reason = QStringLiteral("trajectory_scan_ratio_out_of_range");
            return diagnostic;
        }
        if (delta > 0.02) ++forward;
        else if (delta < -0.02) ++backward;
        if (std::abs(delta) > 0.35) ++jumps;
        ++steps;
    }
    diagnostic.scanRatioDirection = scanRatioDirectionOf(ratios);
    diagnostic.pointOrderJumpRatio = steps > 0 ? float(double(jumps) / double(steps)) : 0.0f;
    const double minorityRatio = steps > 0
        ? double(qMin(forward, backward)) / double(steps) : 1.0;
    diagnostic.scanRatiosMonotonic = diagnostic.scanRatioDirection != 0.0f
        && minorityRatio <= 0.02;
    if (!diagnostic.scanRatiosMonotonic) {
        diagnostic.reason = QStringLiteral("trajectory_scan_ratio_not_monotonic");
        return diagnostic;
    }
    if (diagnostic.pointOrderJumpRatio > 0.05f) {
        diagnostic.reason = QStringLiteral("trajectory_point_order_jump");
        return diagnostic;
    }
    // 3. The dominant travel axis must be available and identical on both
    // frames of the pair.
    const int sourceAxis = source.dominantTravelAxis >= 0
        ? source.dominantTravelAxis
        : dominantAxisOf(source.startBaseFromFlange, source.endBaseFromFlange);
    const int targetAxis = target.dominantTravelAxis >= 0
        ? target.dominantTravelAxis
        : dominantAxisOf(target.startBaseFromFlange, target.endBaseFromFlange);
    diagnostic.travelAxis = sourceAxis;
    diagnostic.travelAxisStable = sourceAxis >= 0 && sourceAxis == targetAxis;
    if (!diagnostic.travelAxisStable) {
        diagnostic.reason = QStringLiteral("trajectory_travel_axis_inconsistent");
        return diagnostic;
    }
    // 4. The longitudinal extent of the point cloud must follow the robot
    // motion direction.
    QVector3D travel(0.0f, 0.0f, 0.0f);
    travel[sourceAxis] = 1.0f;
    const QVector3D perpendicular = sourceAxis == 0 ? QVector3D(0.0f, 1.0f, 0.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    double travelMinimum = std::numeric_limits<double>::max();
    double travelMaximum = std::numeric_limits<double>::lowest();
    double perpendicularMinimum = std::numeric_limits<double>::max();
    double perpendicularMaximum = std::numeric_limits<double>::lowest();
    const qsizetype stride = qMax<qsizetype>(1, source.points->size() / 20000);
    qsizetype seen = 0;
    for (const Point3D &point : *source.points) {
        if (!finitePoint(point)) continue;
        if ((seen++ % stride) != 0) continue;
        const double along = double(point.x) * double(travel.x()) + double(point.y) * double(travel.y());
        const double across = double(point.x) * double(perpendicular.x())
            + double(point.y) * double(perpendicular.y());
        travelMinimum = qMin(travelMinimum, along);
        travelMaximum = qMax(travelMaximum, along);
        perpendicularMinimum = qMin(perpendicularMinimum, across);
        perpendicularMaximum = qMax(perpendicularMaximum, across);
    }
    if (seen == 0 || travelMaximum <= travelMinimum) {
        diagnostic.reason = QStringLiteral("trajectory_extent_mismatch");
        return diagnostic;
    }
    diagnostic.extentAlongTravelMm = float(travelMaximum - travelMinimum);
    if ((travelMaximum - travelMinimum) < (perpendicularMaximum - perpendicularMinimum)) {
        diagnostic.reason = QStringLiteral("trajectory_extent_mismatch");
        return diagnostic;
    }
    // 5. The robot displacement between the two frames must stay consistent
    // with the observed scan length.
    const QVector3D sourceCenter(
        (source.startBaseFromFlange(0, 3) + source.endBaseFromFlange(0, 3)) * 0.5f,
        (source.startBaseFromFlange(1, 3) + source.endBaseFromFlange(1, 3)) * 0.5f,
        (source.startBaseFromFlange(2, 3) + source.endBaseFromFlange(2, 3)) * 0.5f);
    const QVector3D targetCenter(
        (target.startBaseFromFlange(0, 3) + target.endBaseFromFlange(0, 3)) * 0.5f,
        (target.startBaseFromFlange(1, 3) + target.endBaseFromFlange(1, 3)) * 0.5f,
        (target.startBaseFromFlange(2, 3) + target.endBaseFromFlange(2, 3)) * 0.5f);
    const QVector3D displacement = sourceCenter - targetCenter;
    diagnostic.displacementMm = displacement.length();
    if (!std::isfinite(diagnostic.displacementMm)
        || diagnostic.displacementMm > 3.0f * qMax(1.0f, diagnostic.extentAlongTravelMm)) {
        diagnostic.reason = QStringLiteral("trajectory_displacement_inconsistent");
        return diagnostic;
    }
    // 6. A scanner reversal must reverse the point order with it: the relation
    // between the scan order and the travel direction has to stay the same on
    // both frames of the pair.
    const float sourceTravelSign = travelSignOf(source, sourceAxis);
    const float targetTravelSign = travelSignOf(target, targetAxis);
    const float targetRatioDirection = scanRatioDirectionOf(*target.scanRatios);
    if (sourceTravelSign == 0.0f || targetTravelSign == 0.0f
        || diagnostic.scanRatioDirection == 0.0f) {
        diagnostic.reason = QStringLiteral("trajectory_travel_sign_unknown");
        return diagnostic;
    }
    if (targetRatioDirection == 0.0f) {
        diagnostic.reason = QStringLiteral("trajectory_travel_sign_unknown");
        return diagnostic;
    }
    if (sourceTravelSign * diagnostic.scanRatioDirection
        != targetTravelSign * targetRatioDirection) {
        diagnostic.reason = QStringLiteral("trajectory_reversal_not_synced");
        return diagnostic;
    }
    diagnostic.ok = true;
    diagnostic.reason = QStringLiteral("轨迹诊断通过");
    return diagnostic;
}

// ---------------------------------------------------------------------------
// Bounded coarse-to-fine height-residual search
// ---------------------------------------------------------------------------

HeightMapSearchDiagnostic searchHeightMapAlignment(const HeightMap &source,
                                                   const HeightMap &target,
                                                   const HeightMapSearchOptions &options)
{
    HeightMapSearchDiagnostic diagnostic;
    diagnostic.attempted = true;
    diagnostic.candidate.setToIdentity();
    const auto reject = [&diagnostic](const QString &reason) {
        diagnostic.accepted = false;
        diagnostic.reason = reason;
        return diagnostic;
    };
    const auto cancelled = [&options]() {
        return options.isCancelled && options.isCancelled();
    };
    if (cancelled()) return reject(QStringLiteral("已取消"));
    if (!source.ok || !target.ok)
        return reject(QStringLiteral("heightmap_insufficient_data"));
    if (source.width != target.width || source.height != target.height
        || source.width <= 0 || source.height <= 0)
        return reject(QStringLiteral("heightmap_region_invalid"));
    const double resolution = double(target.gridResolutionMm);
    if (!(resolution > 0.0)
        || std::abs(double(source.gridResolutionMm) - resolution) > 1.0e-6)
        return reject(QStringLiteral("heightmap_grid_resolution_invalid"));

    // The robot-initial overlap box is the extent both maps share. Every
    // candidate rebuilds the overlap box from the translated source bounds, so
    // the score region is the intersection of the two boxes and never the
    // joint bounding box of the two frames.
    const double margin = qMax(0.0, double(options.overlapMarginMm));
    const double initialLowX = double(target.localMinimum.x());
    const double initialHighX = double(target.localMaximum.x());
    const double initialLowY = double(target.localMinimum.y());
    const double initialHighY = double(target.localMaximum.y());
    const double sourceMinimumX = double(source.cloudMinimum.x());
    const double sourceMaximumX = double(source.cloudMaximum.x());
    const double sourceMinimumY = double(source.cloudMinimum.y());
    const double sourceMaximumY = double(source.cloudMaximum.y());
    const double targetMinimumX = double(target.cloudMinimum.x());
    const double targetMaximumX = double(target.cloudMaximum.x());
    const double targetMinimumY = double(target.cloudMinimum.y());
    const double targetMaximumY = double(target.cloudMaximum.y());
    const double centerX = 0.5 * (initialLowX + initialHighX);
    const double centerY = 0.5 * (initialLowY + initialHighY);

    QVector<float> residualScratch;
    const auto scoreCandidate = [&](double dx, double dy, double dz, double yaw,
                                    bool rawElevation) {
        ScoreResult result;
        const double cosine = std::cos(yaw);
        const double sine = std::sin(yaw);
        const double candidateLowX = qMax(sourceMinimumX + dx, targetMinimumX) - margin;
        const double candidateHighX = qMin(sourceMaximumX + dx, targetMaximumX) + margin;
        const double candidateLowY = qMax(sourceMinimumY + dy, targetMinimumY) - margin;
        const double candidateHighY = qMin(sourceMaximumY + dy, targetMaximumY) + margin;
        const double regionLowX = qMax(initialLowX, candidateLowX);
        const double regionHighX = qMin(initialHighX, candidateHighX);
        const double regionLowY = qMax(initialLowY, candidateLowY);
        const double regionHighY = qMin(initialHighY, candidateHighY);
        if (!(regionHighX > regionLowX) || !(regionHighY > regionLowY)) return result;
        const int columnBegin = int(std::ceil((regionLowX - double(target.originX)) / resolution));
        const int columnEnd = int(std::floor((regionHighX - double(target.originX)) / resolution));
        const int rowBegin = int(std::ceil((regionLowY - double(target.originY)) / resolution));
        const int rowEnd = int(std::floor((regionHighY - double(target.originY)) / resolution));
        if (columnBegin > columnEnd || rowBegin > rowEnd) return result;
        if (columnBegin < 0 || rowBegin < 0
            || columnEnd >= target.width || rowEnd >= target.height) return result;
        const qsizetype columns = qsizetype(columnEnd - columnBegin + 1);
        const qsizetype rows = qsizetype(rowEnd - rowBegin + 1);
        const qsizetype total = columns * rows;
        const int stride = total > kMaximumScoringCells
            ? qMax(1, int(std::ceil(std::sqrt(double(total) / double(kMaximumScoringCells)))))
            : 1;
        const QVector<float> &channel = rawElevation ? source.elevation : source.detrended;
        residualScratch.clear();
        double sumSource = 0.0, sumTarget = 0.0;
        double sumSourceSquared = 0.0, sumTargetSquared = 0.0, sumProduct = 0.0;
        for (int row = rowBegin; row <= rowEnd; row += stride) {
            const double y = double(target.cellCenterY(row));
            for (int column = columnBegin; column <= columnEnd; column += stride) {
                ++result.regionCells;
                const int cell = target.index(column, row);
                if (target.mask.at(cell) == 0) continue;
                ++result.targetValid;
                // Both sides must be fully observed around the scored cell.
                if (!neighbourhoodValid(target, column, row, 1)) continue;
                const double x = double(target.cellCenterX(column));
                const double relativeX = x - centerX - dx;
                const double relativeY = y - centerY - dy;
                const double sourceX = centerX + relativeX * cosine + relativeY * sine;
                const double sourceY = centerY - relativeX * sine + relativeY * cosine;
                float sourceValue = 0.0f;
                if (!sampleChannel(source, channel, float(sourceX), float(sourceY), &sourceValue))
                    continue;
                ++result.sourceValid;
                const double shifted = double(sourceValue) + (rawElevation ? dz : 0.0);
                const double targetValue = rawElevation
                    ? double(target.elevation.at(cell)) : double(target.detrended.at(cell));
                const double residual = shifted - targetValue;
                if (!std::isfinite(residual)) continue;
                residualScratch.push_back(float(std::abs(residual)));
                sumSource += shifted;
                sumTarget += targetValue;
                sumSourceSquared += shifted * shifted;
                sumTargetSquared += targetValue * targetValue;
                sumProduct += shifted * targetValue;
            }
        }
        if (residualScratch.size() < 16) return result;
        const qsizetype sampleCount = residualScratch.size();
        double robustCost = 0.0;
        for (float absoluteResidual : residualScratch) {
            const double residual = double(absoluteResidual);
            robustCost += residual <= kScoreHuberDeltaMm
                ? 0.5 * residual * residual
                : kScoreHuberDeltaMm * (residual - 0.5 * kScoreHuberDeltaMm);
        }
        result.score = robustCost / double(sampleCount);
        const double count = double(sampleCount);
        const double covarianceNumerator = count * sumProduct - sumSource * sumTarget;
        const double sourceSpread = count * sumSourceSquared - sumSource * sumSource;
        const double targetSpread = count * sumTargetSquared - sumTarget * sumTarget;
        if (sourceSpread > 1.0e-9 && targetSpread > 1.0e-9) {
            const double denominator = std::sqrt(sourceSpread * targetSpread);
            if (denominator > 1.0e-12)
                result.correlation = qBound(-1.0, covarianceNumerator / denominator, 1.0);
        }
        result.valid = std::isfinite(result.score);
        return result;
    };

    const ScoreResult before = scoreCandidate(0.0, 0.0, 0.0, 0.0, false);
    if (!before.valid) return reject(QStringLiteral("heightmap_insufficient_data"));
    diagnostic.scoreBefore = float(before.score);
    diagnostic.correlationBefore = float(before.correlation);
    diagnostic.sourceValidRatio = before.regionCells > 0
        ? float(double(before.sourceValid) / double(before.regionCells)) : 0.0f;
    diagnostic.targetValidRatio = before.regionCells > 0
        ? float(double(before.targetValid) / double(before.regionCells)) : 0.0f;
    diagnostic.sourceValidPoints = before.sourceValid;
    diagnostic.targetValidPoints = before.targetValid;
    if (!options.searchEnabled)
        return reject(QStringLiteral("heightmap_search_disabled"));

    // Strict budget, never wider than the retained recovery budget.
    const double strictTranslation = qMin(qMax(0.0, double(options.maximumTranslationMm)),
                                          qMax(0.0, double(options.recoveryTranslationMm)));
    const double strictYaw = qMin(qMax(0.0, double(options.maximumYawDegrees)),
                                  qMax(0.0, double(options.recoveryYawDegrees)));
    if (!(strictTranslation > 0.0))
        return reject(QStringLiteral("heightmap_candidate_exceeds_budget"));

    // ---- stage I: dx / dy only ----
    const double coarseStep = qMax(1.0, 2.0 * resolution);
    const int coarseSteps = int(std::floor(strictTranslation / coarseStep));
    struct Candidate { double dx; double dy; double score; };
    QVector<Candidate> coarse;
    for (int stepX = -coarseSteps; stepX <= coarseSteps; ++stepX) {
        if (cancelled()) return reject(QStringLiteral("已取消"));
        for (int stepY = -coarseSteps; stepY <= coarseSteps; ++stepY) {
            const double dx = double(stepX) * coarseStep;
            const double dy = double(stepY) * coarseStep;
            if (std::sqrt(dx * dx + dy * dy) > strictTranslation + 1.0e-6) continue;
            const ScoreResult scored = scoreCandidate(dx, dy, 0.0, 0.0, false);
            ++diagnostic.candidateCount;
            if (!scored.valid) continue;
            coarse.push_back(Candidate{dx, dy, scored.score});
        }
    }
    if (coarse.isEmpty()) return reject(QStringLiteral("heightmap_insufficient_data"));
    int bestIndex = 0;
    for (int index = 1; index < coarse.size(); ++index)
        if (coarse.at(index).score < coarse.at(bestIndex).score) bestIndex = index;
    // Candidate uniqueness: the runner-up must be separated from the winner by
    // more than the ambiguity margin, and a neighbour of the same optimum does
    // not count as a runner-up.
    double runnerUp = std::numeric_limits<double>::max();
    for (int index = 0; index < coarse.size(); ++index) {
        if (index == bestIndex) continue;
        const double offsetX = std::abs(coarse.at(index).dx - coarse.at(bestIndex).dx);
        const double offsetY = std::abs(coarse.at(index).dy - coarse.at(bestIndex).dy);
        if (offsetX <= coarseStep + 1.0e-6 && offsetY <= coarseStep + 1.0e-6) continue;
        runnerUp = qMin(runnerUp, coarse.at(index).score);
    }
    if (std::isfinite(runnerUp)) {
        const double reference = qMax(runnerUp, 1.0e-9);
        diagnostic.ambiguous = (runnerUp - coarse.at(bestIndex).score) / reference
            < kAmbiguitySeparation;
    }
    if (diagnostic.ambiguous)
        return reject(QStringLiteral("heightmap_candidate_ambiguous"));
    double bestX = coarse.at(bestIndex).dx;
    double bestY = coarse.at(bestIndex).dy;
    // Two refinement passes, each one step of the previous level.
    const auto refine = [&](double radius, double step) {
        if (!(step > 0.0) || !(radius > 0.0)) return;
        const int steps = qMax(1, int(std::ceil(radius / step)));
        double bestScore = scoreCandidate(bestX, bestY, 0.0, 0.0, false).score;
        for (int stepX = -steps; stepX <= steps; ++stepX) {
            if (cancelled()) return;
            for (int stepY = -steps; stepY <= steps; ++stepY) {
                const double dx = bestX + double(stepX) * step;
                const double dy = bestY + double(stepY) * step;
                if (std::sqrt(dx * dx + dy * dy) > strictTranslation + 1.0e-6) continue;
                const ScoreResult scored = scoreCandidate(dx, dy, 0.0, 0.0, false);
                ++diagnostic.candidateCount;
                if (!scored.valid) continue;
                if (scored.score < bestScore) { bestScore = scored.score; bestX = dx; bestY = dy; }
            }
        }
    };
    refine(coarseStep, qMax(0.25, resolution * 0.5));
    if (cancelled()) return reject(QStringLiteral("已取消"));
    refine(qMax(0.25, resolution * 0.5), qMax(0.1, resolution * 0.25));
    if (cancelled()) return reject(QStringLiteral("已取消"));

    // ---- stage II: a small dz window, then a small yaw window ----
    const double remainingTranslation = std::sqrt(qMax(0.0, strictTranslation * strictTranslation
        - (bestX * bestX + bestY * bestY)));
    const double dzRange = qMin(3.0, remainingTranslation);
    const double dzStep = 0.25;
    double bestZ = 0.0;
    if (dzRange > 0.0) {
        const int zSteps = int(std::floor(dzRange / dzStep));
        double bestZScore = std::numeric_limits<double>::max();
        for (int step = -zSteps; step <= zSteps; ++step) {
            if (cancelled()) return reject(QStringLiteral("已取消"));
            const double dz = double(step) * dzStep;
            const ScoreResult scored = scoreCandidate(bestX, bestY, dz, 0.0, true);
            ++diagnostic.candidateCount;
            if (!scored.valid) continue;
            if (scored.score < bestZScore) { bestZScore = scored.score; bestZ = dz; }
        }
    }
    const double yawRange = qMin(0.6, strictYaw);
    const double yawStep = 0.15;
    double bestYaw = 0.0;
    if (yawRange > 0.0) {
        const int yawSteps = int(std::floor(yawRange / yawStep));
        // A yaw only competes with the in-plane solution when the translation is
        // allowed to follow it, so every yaw candidate re-centres dx/dy inside a
        // single half-resolution step before it is scored.
        const double recentreStep = qMax(0.1, resolution * 0.5);
        const ScoreResult referenceScore = scoreCandidate(bestX, bestY, bestZ, 0.0, false);
        double bestYawScore = referenceScore.valid ? referenceScore.score
                                                   : std::numeric_limits<double>::max();
        double acceptedX = bestX;
        double acceptedY = bestY;
        for (int step = -yawSteps; step <= yawSteps; ++step) {
            if (cancelled()) return reject(QStringLiteral("已取消"));
            const double yaw = qDegreesToRadians(double(step) * yawStep);
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                    const double dx = bestX + double(offsetX) * recentreStep;
                    const double dy = bestY + double(offsetY) * recentreStep;
                    const ScoreResult scored = scoreCandidate(dx, dy, bestZ, yaw, false);
                    ++diagnostic.candidateCount;
                    if (!scored.valid) continue;
                    if (scored.score < bestYawScore - 1.0e-12) {
                        bestYawScore = scored.score;
                        bestYaw = double(step) * yawStep;
                        acceptedX = dx;
                        acceptedY = dy;
                    }
                }
            }
        }
        bestX = acceptedX;
        bestY = acceptedY;
    }

    const ScoreResult after = scoreCandidate(bestX, bestY, bestZ, qDegreesToRadians(bestYaw), false);
    if (!after.valid) return reject(QStringLiteral("heightmap_insufficient_data"));
    diagnostic.scoreAfter = float(after.score);
    diagnostic.correlationAfter = float(after.correlation);
    diagnostic.sourceValidRatio = after.regionCells > 0
        ? float(double(after.sourceValid) / double(after.regionCells)) : 0.0f;
    diagnostic.targetValidRatio = after.regionCells > 0
        ? float(double(after.targetValid) / double(after.regionCells)) : 0.0f;
    diagnostic.sourceValidPoints = after.sourceValid;
    diagnostic.targetValidPoints = after.targetValid;
    diagnostic.translationX = float(bestX);
    diagnostic.translationY = float(bestY);
    diagnostic.translationZ = float(bestZ);
    diagnostic.yawDegrees = float(bestYaw);
    diagnostic.relativeImprovement = before.score > 1.0e-9
        ? float((before.score - after.score) / before.score) : 0.0f;

    // No candidate may cross the strict budget, and the accepted one must be
    // strictly better than the robot initial value by the configured margin.
    const double translation = std::sqrt(bestX * bestX + bestY * bestY + bestZ * bestZ);
    if (translation > strictTranslation + 1.0e-4
        || std::abs(bestYaw) > strictYaw + 1.0e-6) {
        diagnostic.exceededStrictBudget = true;
        return reject(QStringLiteral("heightmap_candidate_exceeds_budget"));
    }
    if (!(after.score < before.score)
        || diagnostic.relativeImprovement < options.minimumRelativeImprovement)
        return reject(QStringLiteral("heightmap_insufficient_improvement"));

    QMatrix4x4 candidate;
    candidate.setToIdentity();
    candidate.translate(float(bestX), float(bestY), float(bestZ));
    candidate.translate(float(centerX), float(centerY), 0.0f);
    candidate.rotate(float(bestYaw), 0.0f, 0.0f, 1.0f);
    candidate.translate(float(-centerX), float(-centerY), 0.0f);
    diagnostic.candidate = candidate;
    diagnostic.accepted = true;
    diagnostic.reason = QStringLiteral("高程图候选通过严格预算与相对改善门槛");
    return diagnostic;
}

} // namespace pointcloud
