#include <pcv/stitching/multiframe_registration.h>
#include <pcv/stitching/heightmap_registration.h>
#include <pcv/filtering/downsample.h>

#include <QElapsedTimer>
#include <QFuture>
#include <QQuaternion>
#include <QThreadPool>
#include <QStringList>
#include <QVector3D>
#include <QtConcurrent>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace pointcloud {
namespace {

struct GridKey {
    qint64 x = 0;
    qint64 y = 0;
    qint64 z = 0;
    bool operator==(const GridKey &other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridHash {
    size_t operator()(const GridKey &key) const noexcept {
        return size_t(quint64(key.x) * 73856093ULL
            ^ quint64(key.y) * 19349663ULL
            ^ quint64(key.z) * 83492791ULL);
    }
};

struct Bounds {
    QVector3D minimum{std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max()};
    QVector3D maximum{std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest()};
    bool valid = false;
    bool oriented = false;
    QVector3D origin;
    QVector3D axisU{1.0f,0.0f,0.0f};
    QVector3D axisV{0.0f,1.0f,0.0f};
    QVector3D axisN{0.0f,0.0f,1.0f};
};

struct ConvertedCloud {
    QVector<Point3D> full;
    QVector<Point3D> sample;
    QVector<qsizetype> sourceIndices;
    QVector<float> scanRatios;
    qsizetype declaredCount = 0;
    qsizetype rejectedBasic = 0;
    qsizetype rejectedRange = 0;
    float inputYMinimum = std::numeric_limits<float>::max();
    float inputYMaximum = std::numeric_limits<float>::lowest();
};

struct SpatialIndex {
    float cellSize = 1.0f;
    std::unordered_map<GridKey, QVector<int>, GridHash> cells;
};

struct PairStageTimes {
    qint64 preprocessMs = 0;
    qint64 coarseSearchMs = 0;
    qint64 levelMs[3] = {0, 0, 0};
    qint64 structuralValidationMs = 0;
    // Sub-stage accounting inside the level loop / attempt.
    qint64 targetLevelCacheMs = 0;  // per-level target downsample+normals+index prep
    qint64 correspondenceMs = 0;    // estimateIncrement calls
    qint64 boundaryCheckMs = 0;     // step-factor overlap boundary trials
};

// Accumulates wall-clock time of the single currently active registration
// stage into a PairStageTimes bucket. Region boundaries are marked with
// start()/stop(); if the pair attempt returns while a region is still active,
// the destructor still charges the elapsed time to that stage.
class RegionTimer {
    QElapsedTimer timer;
    qint64 *target = nullptr;
    bool active = false;
public:
    void start(qint64 *destination)
    {
        target = destination;
        timer.restart();
        active = true;
    }
    void stop()
    {
        if (active && target) *target += timer.elapsed();
        active = false;
        target = nullptr;
    }
    ~RegionTimer() { stop(); }
};

struct Increment {
    QMatrix4x4 matrix;
    int correspondences = 0;
    int uniqueReferences = 0;
    float rmse = 0.0f;
    float xyRmse = 0.0f;
    float zRmse = 0.0f;
    float conditionRatio = 0.0f;
    int observableDof = 0;
    bool degenerate = false;
    std::array<std::array<double,6>,6> eigenvectors{};
    std::array<double,6> eigenvalues{};
    std::array<double,6> normalizedUpdate{};
    QVector3D rotationCenter;
    double rotationScale = 1.0;
    bool ok = false;
};

struct FeatureResidualOptions {
    bool enabled = false;
    QVector3D normal;
    QVector3D origin;
    QVector3D axisU;
    QVector3D axisV;
    float offset = 0.0f;
    float planeExclusion = 0.0f;
    float boundaryWidth = 2.0f;
    Bounds boundaryBounds;
};

bool finitePoint(const Point3D &point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool nonzeroPoint(const Point3D &point) {
    return std::abs(point.x) > 1.0e-12f
        || std::abs(point.y) > 1.0e-12f
        || std::abs(point.z) > 1.0e-12f;
}

QVector3D vectorOf(const Point3D &point) {
    return QVector3D(point.x, point.y, point.z);
}

GridKey gridKey(const QVector3D &point, float cellSize) {
    return {qint64(std::floor(point.x() / cellSize)),
            qint64(std::floor(point.y() / cellSize)),
            qint64(std::floor(point.z() / cellSize))};
}

GridKey gridKey(const Point3D &point, float cellSize) {
    return gridKey(vectorOf(point), cellSize);
}

Bounds cloudBounds(const QVector<Point3D> &points) {
    Bounds result;
    for (const Point3D &point : points) {
        if (!finitePoint(point)) continue;
        result.minimum.setX(qMin(result.minimum.x(), point.x));
        result.minimum.setY(qMin(result.minimum.y(), point.y));
        result.minimum.setZ(qMin(result.minimum.z(), point.z));
        result.maximum.setX(qMax(result.maximum.x(), point.x));
        result.maximum.setY(qMax(result.maximum.y(), point.y));
        result.maximum.setZ(qMax(result.maximum.z(), point.z));
        result.valid = true;
    }
    return result;
}

bool insideBounds(const QVector3D &point, const Bounds &bounds) {
    if (!bounds.valid) return false;
    if (bounds.oriented) {
        const QVector3D delta = point - bounds.origin;
        const QVector3D local(QVector3D::dotProduct(delta, bounds.axisU),
                              QVector3D::dotProduct(delta, bounds.axisV),
                              QVector3D::dotProduct(delta, bounds.axisN));
        return local.x()>=bounds.minimum.x()&&local.x()<=bounds.maximum.x()
            &&local.y()>=bounds.minimum.y()&&local.y()<=bounds.maximum.y()
            &&local.z()>=bounds.minimum.z()&&local.z()<=bounds.maximum.z();
    }
    return point.x()>=bounds.minimum.x()&&point.x()<=bounds.maximum.x()
        &&point.y()>=bounds.minimum.y()&&point.y()<=bounds.maximum.y()
        &&point.z()>=bounds.minimum.z()&&point.z()<=bounds.maximum.z();
}

int countInsideBounds(const QVector<Point3D> &points, const Bounds &bounds) {
    int count=0;
    for(const Point3D &point:points)if(insideBounds(vectorOf(point),bounds))++count;
    return count;
}

QString boundsText(const Bounds &bounds) {
    if (!bounds.valid) return QStringLiteral("invalid");
    return QStringLiteral("min(%1,%2,%3) max(%4,%5,%6)")
        .arg(bounds.minimum.x(), 0, 'g', 8)
        .arg(bounds.minimum.y(), 0, 'g', 8)
        .arg(bounds.minimum.z(), 0, 'g', 8)
        .arg(bounds.maximum.x(), 0, 'g', 8)
        .arg(bounds.maximum.y(), 0, 'g', 8)
        .arg(bounds.maximum.z(), 0, 'g', 8);
}

QVector<Point3D> voxelDownsample(const QVector<Point3D> &points, float voxel) {
    return pcv::detail::filtering::voxelDownsample(
        points, voxel, pcv::detail::filtering::VoxelRepresentative::Centroid).points;
}

QVector<Point3D> cropCloud(const QVector<Point3D> &points,
                           const QVector3D &low, const QVector3D &high) {
    QVector<Point3D> result;
    for (const Point3D &point : points) {
        if (point.x < low.x() || point.x > high.x()
            || point.y < low.y() || point.y > high.y()
            || point.z < low.z() || point.z > high.z()) continue;
        result.push_back(point);
    }
    return result;
}

SpatialIndex buildIndex(const QVector<Point3D> &points, float cellSize) {
    SpatialIndex result;
    result.cellSize = cellSize;
    result.cells.reserve(size_t(points.size()));
    for (int index = 0; index < points.size(); ++index)
        result.cells[gridKey(points[index], cellSize)].push_back(index);
    return result;
}

void smallestEigenvector(const double covariance[3][3], QVector3D *normal) {
    double matrix[3][3];
    double vectors[3][3]{{1,0,0},{0,1,0},{0,0,1}};
    for (int row=0;row<3;++row) for(int column=0;column<3;++column)
        matrix[row][column]=covariance[row][column];
    for (int iteration=0;iteration<24;++iteration) {
        int p=0,q=1;
        if(std::abs(matrix[0][2])>std::abs(matrix[p][q])){p=0;q=2;}
        if(std::abs(matrix[1][2])>std::abs(matrix[p][q])){p=1;q=2;}
        if(std::abs(matrix[p][q])<1.0e-12)break;
        const double angle=0.5*std::atan2(2.0*matrix[p][q],matrix[q][q]-matrix[p][p]);
        const double cosine=std::cos(angle),sine=std::sin(angle);
        for(int k=0;k<3;++k){const double a=matrix[p][k],b=matrix[q][k];matrix[p][k]=cosine*a-sine*b;matrix[q][k]=sine*a+cosine*b;}
        for(int k=0;k<3;++k){const double a=matrix[k][p],b=matrix[k][q];matrix[k][p]=cosine*a-sine*b;matrix[k][q]=sine*a+cosine*b;const double va=vectors[k][p],vb=vectors[k][q];vectors[k][p]=cosine*va-sine*vb;vectors[k][q]=sine*va+cosine*vb;}
    }
    int smallest=0;
    if(matrix[1][1]<matrix[smallest][smallest])smallest=1;
    if(matrix[2][2]<matrix[smallest][smallest])smallest=2;
    *normal=QVector3D(float(vectors[0][smallest]),float(vectors[1][smallest]),float(vectors[2][smallest]));
    if(normal->lengthSquared()>1.0e-12f)normal->normalize();
}

void estimateNormals(QVector<Point3D> *points, float radius) {
    if (!points || points->isEmpty()) return;
    const SpatialIndex index = buildIndex(*points, radius);
    const float radiusSquared = radius * radius;
    for (int pointIndex=0; pointIndex<points->size(); ++pointIndex) {
        const Point3D center=points->at(pointIndex);
        const GridKey key=gridKey(center,radius);
        QVector<int> neighbors;
        neighbors.reserve(50);
        for(qint64 z=-1;z<=1&&neighbors.size()<50;++z)
            for(qint64 y=-1;y<=1&&neighbors.size()<50;++y)
                for(qint64 x=-1;x<=1&&neighbors.size()<50;++x){
                    const auto found=index.cells.find({key.x+x,key.y+y,key.z+z});
                    if(found==index.cells.end())continue;
                    for(int candidate:found->second){const QVector3D difference=vectorOf(points->at(candidate))-vectorOf(center);if(difference.lengthSquared()<=radiusSquared){neighbors.push_back(candidate);if(neighbors.size()>=50)break;}}
                }
        if(neighbors.size()<3)continue;
        QVector3D mean;
        for(int candidate:neighbors)mean+=vectorOf(points->at(candidate));
        mean/=float(neighbors.size());
        double covariance[3][3]{};
        for(int candidate:neighbors){const QVector3D d=vectorOf(points->at(candidate))-mean;const double value[3]{d.x(),d.y(),d.z()};for(int row=0;row<3;++row)for(int column=0;column<3;++column)covariance[row][column]+=value[row]*value[column];}
        QVector3D normal;
        smallestEigenvector(covariance,&normal);
        Point3D &point=(*points)[pointIndex];
        point.nx=normal.x();point.ny=normal.y();point.nz=normal.z();
    }
}

bool solve6(double matrix[6][6], double rhs[6], double result[6]) {
    for(int column=0;column<6;++column){int pivot=column;for(int row=column+1;row<6;++row)if(std::abs(matrix[row][column])>std::abs(matrix[pivot][column]))pivot=row;if(std::abs(matrix[pivot][column])<1.0e-12)return false;if(pivot!=column){for(int j=0;j<6;++j)std::swap(matrix[column][j],matrix[pivot][j]);std::swap(rhs[column],rhs[pivot]);}const double scale=matrix[column][column];for(int j=column;j<6;++j)matrix[column][j]/=scale;rhs[column]/=scale;for(int row=0;row<6;++row){if(row==column)continue;const double factor=matrix[row][column];for(int j=column;j<6;++j)matrix[row][j]-=factor*matrix[column][j];rhs[row]-=factor*rhs[column];}}
    for(int index=0;index<6;++index)result[index]=rhs[index];
    return true;
}

void symmetricEigen6(const double input[6][6], double values[6], double vectors[6][6]) {
    double matrix[6][6];
    for(int row=0;row<6;++row)for(int column=0;column<6;++column){matrix[row][column]=input[row][column];vectors[row][column]=row==column?1.0:0.0;}
    for(int iteration=0;iteration<96;++iteration){
        int p=0,q=1;double largest=std::abs(matrix[p][q]);
        for(int row=0;row<6;++row)for(int column=row+1;column<6;++column)if(std::abs(matrix[row][column])>largest){largest=std::abs(matrix[row][column]);p=row;q=column;}
        if(largest<1.0e-12)break;
        const double angle=0.5*std::atan2(2.0*matrix[p][q],matrix[q][q]-matrix[p][p]);
        const double cosine=std::cos(angle),sine=std::sin(angle);
        for(int k=0;k<6;++k){const double a=matrix[p][k],b=matrix[q][k];matrix[p][k]=cosine*a-sine*b;matrix[q][k]=sine*a+cosine*b;}
        for(int k=0;k<6;++k){const double a=matrix[k][p],b=matrix[k][q];matrix[k][p]=cosine*a-sine*b;matrix[k][q]=sine*a+cosine*b;const double va=vectors[k][p],vb=vectors[k][q];vectors[k][p]=cosine*va-sine*vb;vectors[k][q]=sine*va+cosine*vb;}
    }
    for(int index=0;index<6;++index)values[index]=matrix[index][index];
}

struct CorrespondencePair { QVector3D source, target, normal; int targetIndex = -1; };

// Fixed 27-cell neighbourhood offsets (z,y,x in {-1,0,1}) reused by every
// correspondence and distance query instead of building triple loops.
const std::array<GridKey, 27> &neighborOffsets27() {
    static const std::array<GridKey, 27> offsets = [] {
        std::array<GridKey, 27> table{};
        int slot = 0;
        for (qint64 z = -1; z <= 1; ++z)
            for (qint64 y = -1; y <= 1; ++y)
                for (qint64 x = -1; x <= 1; ++x)
                    table[size_t(slot++)] = GridKey{x, y, z};
        return table;
    }();
    return offsets;
}

struct CorrespondenceChunk {
    QVector<CorrespondencePair> pairs;
    int rejectedByNormal = 0;
};

// One chunk of the correspondence search. Everything it reads is const (the
// source cloud, the target cloud and the index) and it writes only its private
// chunk, so several chunks may run concurrently. The caller merges them in
// index order, which reproduces the serial pair order exactly and therefore
// keeps the accumulated normal equations bit-for-bit identical.
CorrespondenceChunk findCorrespondences(const QVector<Point3D> &source,
                                        const QVector<Point3D> &target,
                                        const SpatialIndex &targetIndex,
                                        float maximumDistance,
                                        const Bounds &actualOverlap,
                                        float minimumNormalDot,
                                        qsizetype begin,
                                        qsizetype end)
{
    CorrespondenceChunk chunk;
    const float limitSquared = maximumDistance * maximumDistance;
    const std::array<GridKey, 27> &offsets = neighborOffsets27();
    chunk.pairs.reserve(end - begin);
    for (qsizetype index = begin; index < end; ++index) {
        const Point3D &point = source.at(index);
        if (!insideBounds(vectorOf(point), actualOverlap)) continue;
        const GridKey key = gridKey(point, maximumDistance);
        float best = limitSquared;
        int selected = -1;
        for (const GridKey &offset : offsets) {
            const auto found = targetIndex.cells.find(
                {key.x + offset.x, key.y + offset.y, key.z + offset.z});
            if (found == targetIndex.cells.end()) continue;
            for (int candidate : found->second) {
                const float distance = (vectorOf(point) - vectorOf(target.at(candidate))).lengthSquared();
                if (distance < best) { best = distance; selected = candidate; }
            }
        }
        if (selected < 0) continue;
        const Point3D &reference = target.at(selected);
        QVector3D normal(reference.nx, reference.ny, reference.nz);
        if (normal.lengthSquared() <= 1.0e-8f) continue;
        normal.normalize();
        // Normal compatibility: the nearest target point of a pair that jumped
        // across an edge or a step points somewhere else. Both normals must be
        // available before the gate can judge the pair.
        if (minimumNormalDot > -1.0f) {
            const QVector3D sourceNormal(point.nx, point.ny, point.nz);
            if (sourceNormal.lengthSquared() > 1.0e-8f
                && QVector3D::dotProduct(sourceNormal.normalized(), normal) < minimumNormalDot) {
                ++chunk.rejectedByNormal;
                continue;
            }
        }
        chunk.pairs.push_back({vectorOf(point), vectorOf(reference), normal, selected});
    }
    return chunk;
}

Increment estimateIncrement(const QVector<Point3D> &source,
                            const QVector<Point3D> &target,
                            const SpatialIndex &targetIndex,
                            float maximumDistance,
                            const Bounds &actualOverlap,
                            QVector<CorrespondencePair> *pairsScratch = nullptr,
                            std::unordered_set<int> *uniqueScratch = nullptr,
                            const FeatureResidualOptions *featureResiduals = nullptr,
                            float normalCompatibilityDegrees = 0.0f,
                            float trimFraction = 1.0f,
                            QVector<float> *residualScratch = nullptr,
                            int *normalRejected = nullptr,
                            int parallelTasks = 1,
                            bool restrictedDof = false) {
    // The reference point set is always cropped to actualOverlap before it is
    // downsampled and indexed, so every indexed candidate is already inside
    // the overlap box and the per-candidate insideBounds() check is removed.
    QVector<CorrespondencePair> localPairs;
    QVector<CorrespondencePair> &pairs = pairsScratch ? *pairsScratch : localPairs;
    pairs.clear();
    const float minimumNormalDot = normalCompatibilityDegrees>0.0f
        ? float(std::cos(double(qDegreesToRadians(qBound(0.0f,normalCompatibilityDegrees,180.0f)))))
        : -1.0f;
    int rejectedByNormal = 0;
    // The search is the dominant cost of every iteration. It is split into
    // ordered chunks so it can use several workers; the merge below runs on the
    // calling thread and reproduces the serial pair order exactly.
    constexpr qsizetype minimumParallelPoints = 20000;
    if(parallelTasks>1 && source.size()>=minimumParallelPoints){
        const int chunkCount=qMin<int>(parallelTasks*4,int(source.size()));
        const qsizetype chunkSize=(source.size()+chunkCount-1)/chunkCount;
        QThreadPool pool;
        pool.setMaxThreadCount(parallelTasks);
        QVector<QFuture<CorrespondenceChunk>> futures;
        futures.reserve(chunkCount);
        for(int chunk=0;chunk<chunkCount;++chunk){
            const qsizetype begin=qsizetype(chunk)*chunkSize;
            const qsizetype end=qMin(begin+chunkSize,source.size());
            if(begin>=end)continue;
            futures.push_back(QtConcurrent::run(&pool,
                [&source,&target,&targetIndex,maximumDistance,&actualOverlap,minimumNormalDot,begin,end]{
                    return findCorrespondences(source,target,targetIndex,maximumDistance,
                                               actualOverlap,minimumNormalDot,begin,end);
                }));
        }
        for(QFuture<CorrespondenceChunk> &future:futures){
            const CorrespondenceChunk chunk=future.result();
            rejectedByNormal+=chunk.rejectedByNormal;
            for(const CorrespondencePair &pair:chunk.pairs)pairs.push_back(pair);
        }
    }else{
        CorrespondenceChunk chunk=findCorrespondences(source,target,targetIndex,
            maximumDistance,actualOverlap,minimumNormalDot,0,source.size());
        rejectedByNormal=chunk.rejectedByNormal;
        pairs=std::move(chunk.pairs);
    }
    if(normalRejected)*normalRejected+=rejectedByNormal;
    Increment result;
    result.correspondences=pairs.size();
    if(pairs.size()<100)return result;
    std::unordered_set<int> localUnique;
    std::unordered_set<int> &unique = uniqueScratch ? *uniqueScratch : localUnique;
    unique.clear();
    QVector3D sourceCentroid;
    for(const CorrespondencePair &pair:pairs)sourceCentroid+=pair.source;
    sourceCentroid/=float(pairs.size());
    double squaredRadius=0.0;
    for(const CorrespondencePair &pair:pairs)squaredRadius+=(pair.source-sourceCentroid).lengthSquared();
    const double rotationScale=qMax(1.0,std::sqrt(squaredRadius/double(pairs.size())));
    double normalMatrix[6][6]{},rhs[6]{},weightSum=0.0;
    const auto isFeatureTarget = [&](const CorrespondencePair &pair) {
        if (!featureResiduals || !featureResiduals->enabled) return false;
        if (std::abs(QVector3D::dotProduct(featureResiduals->normal, pair.target)
                     - featureResiduals->offset) > featureResiduals->planeExclusion)
            return true;
        if (!featureResiduals->boundaryBounds.valid) return false;
        const QVector3D delta = pair.target - featureResiduals->origin;
        const float u = QVector3D::dotProduct(delta, featureResiduals->axisU);
        const float v = QVector3D::dotProduct(delta, featureResiduals->axisV);
        const Bounds &bounds = featureResiduals->boundaryBounds;
        return u - bounds.minimum.x() <= featureResiduals->boundaryWidth
            || bounds.maximum.x() - u <= featureResiduals->boundaryWidth
            || v - bounds.minimum.y() <= featureResiduals->boundaryWidth
            || bounds.maximum.y() - v <= featureResiduals->boundaryWidth;
    };
    const auto accumulateResidual = [&](const QVector3D &axis, const QVector3D &sourcePoint,
                                        const QVector3D &targetPoint, double weight) {
        const double residual = QVector3D::dotProduct(axis, sourcePoint - targetPoint);
        const QVector3D rotation = QVector3D::crossProduct(sourcePoint-sourceCentroid,axis)
            / float(rotationScale);
        const double jacobian[6]{rotation.x(),rotation.y(),rotation.z(),axis.x(),axis.y(),axis.z()};
        for(int row=0;row<6;++row){
            rhs[row]-=weight*jacobian[row]*residual;
            for(int column=0;column<6;++column)
                normalMatrix[row][column]+=weight*jacobian[row]*jacobian[column];
        }
    };
    // Restricted (2.5D) increment: the increment is parameterised by a yaw about
    // the robot-base Z axis through the source centroid plus tx/ty/tz. The yaw
    // column is the Z component of the rotation Jacobian, so the two
    // out-of-plane rotation components are never solved for and the full 6 DoF
    // is not opened.
    const auto accumulateRestrictedResidual = [&](const QVector3D &axis,
                                                  const QVector3D &sourcePoint,
                                                  const QVector3D &targetPoint,
                                                  double weight) {
        const double residual = QVector3D::dotProduct(axis, sourcePoint - targetPoint);
        const QVector3D rotation = QVector3D::crossProduct(sourcePoint-sourceCentroid,axis)
            / float(rotationScale);
        const double jacobian[4]{double(rotation.z()),double(axis.x()),double(axis.y()),double(axis.z())};
        for(int row=0;row<4;++row){
            rhs[row]-=weight*jacobian[row]*residual;
            for(int column=0;column<4;++column)
                normalMatrix[row][column]+=weight*jacobian[row]*jacobian[column];
        }
    };
    // Trimmed weighting: on a pair the other view only partly observed, the tail
    // of the residual distribution is dominated by correspondences that do not
    // really exist, so only the best fraction is allowed into the normal
    // equations. The cutoff comes from an O(N) order statistic, so no full sort
    // is paid per iteration.
    const float trim = qBound(0.05f,trimFraction,1.0f);
    double residualLimit = std::numeric_limits<double>::max();
    if(trim<0.999f && pairs.size()>=100){
        QVector<float> localResiduals;
        QVector<float> &residuals = residualScratch ? *residualScratch : localResiduals;
        residuals.clear();
        residuals.reserve(pairs.size());
        for(const CorrespondencePair &pair:pairs){
            const QVector3D difference=pair.source-pair.target;
            residuals.push_back(float(isFeatureTarget(pair)
                ? difference.length()/maximumDistance
                : std::abs(QVector3D::dotProduct(pair.normal,difference))/maximumDistance));
        }
        const int keep=qBound(1,int(std::ceil(double(trim)*double(residuals.size()))),
                              int(residuals.size()));
        if(keep<int(residuals.size())){
            std::nth_element(residuals.begin(),residuals.begin()+(keep-1),residuals.end());
            residualLimit=double(residuals[keep-1]);
        }
    }
    for(const CorrespondencePair &pair:pairs){
        unique.insert(pair.targetIndex);
        const QVector3D difference = pair.source-pair.target;
        const bool pointToPoint = !restrictedDof && isFeatureTarget(pair);
        const double normalized = pointToPoint
            ? difference.length()/maximumDistance
            : std::abs(QVector3D::dotProduct(pair.normal,difference))/maximumDistance;
        if(normalized>=1.0)continue;
        if(normalized>residualLimit)continue;
        const double weight=std::pow(1.0-normalized*normalized,2.0);
        if (restrictedDof) {
            accumulateRestrictedResidual(pair.normal,pair.source,pair.target,weight);
            weightSum+=weight;
        } else if (pointToPoint) {
            accumulateResidual(QVector3D(1,0,0),pair.source,pair.target,weight);
            accumulateResidual(QVector3D(0,1,0),pair.source,pair.target,weight);
            accumulateResidual(QVector3D(0,0,1),pair.source,pair.target,weight);
            weightSum+=3.0*weight;
        } else {
            accumulateResidual(pair.normal,pair.source,pair.target,weight);
            weightSum+=weight;
        }
    }
    if(weightSum<=1.0e-9)return result;
    double eigenvalues[6]{},eigenvectors[6][6]{};
    symmetricEigen6(normalMatrix,eigenvalues,eigenvectors);
    double maximumEigenvalue=0.0;
    for(double value:eigenvalues)maximumEigenvalue=qMax(maximumEigenvalue,value);
    if(maximumEigenvalue<=1.0e-12)return result;
    constexpr double relativeEigenThreshold=1.0e-3;
    double solution[6]{};double minimumKept=maximumEigenvalue;
    for(int direction=0;direction<6;++direction){
        result.eigenvalues[size_t(direction)]=eigenvalues[direction]/maximumEigenvalue;
        for(int row=0;row<6;++row)result.eigenvectors[size_t(row)][size_t(direction)]=eigenvectors[row][direction];
        if(eigenvalues[direction]<=maximumEigenvalue*relativeEigenThreshold)continue;
        ++result.observableDof;minimumKept=qMin(minimumKept,eigenvalues[direction]);
        double projection=0.0;for(int row=0;row<6;++row)projection+=eigenvectors[row][direction]*rhs[row];
        const double coefficient=projection/eigenvalues[direction];
        for(int row=0;row<6;++row)solution[row]+=eigenvectors[row][direction]*coefficient;
    }
    if(result.observableDof<1)return result;
    // In restricted mode only four directions exist, so an increment that solves
    // all of them is complete; the two padded directions are never observable.
    result.degenerate=result.observableDof<(restrictedDof?4:6);
    result.conditionRatio=float(minimumKept/maximumEigenvalue);
    for(int index=0;index<6;++index)result.normalizedUpdate[size_t(index)]=solution[index];
    result.rotationCenter=sourceCentroid;
    result.rotationScale=rotationScale;
    result.matrix.setToIdentity();
    if (restrictedDof) {
        const double yaw=solution[0]/rotationScale;
        QQuaternion yawRotation;
        if(std::abs(yaw)>1.0e-12)
            yawRotation=QQuaternion::fromAxisAndAngle(QVector3D(0,0,1),float(qRadiansToDegrees(yaw)));
        result.matrix.translate(sourceCentroid);result.matrix.rotate(yawRotation);result.matrix.translate(-sourceCentroid);
        result.matrix(0,3)+=float(solution[1]);result.matrix(1,3)+=float(solution[2]);result.matrix(2,3)+=float(solution[3]);
    } else {
        const QVector3D rotationVector{float(solution[0]/rotationScale),float(solution[1]/rotationScale),float(solution[2]/rotationScale)};
        const float angle=rotationVector.length();
        QQuaternion quaternion;
        if(angle>1.0e-12f)quaternion=QQuaternion::fromAxisAndAngle(rotationVector/angle,qRadiansToDegrees(angle));
        result.matrix.translate(sourceCentroid);result.matrix.rotate(quaternion);result.matrix.translate(-sourceCentroid);
        result.matrix(0,3)+=float(solution[3]);result.matrix(1,3)+=float(solution[4]);result.matrix(2,3)+=float(solution[5]);
    }
    double squared=0.0,xySquared=0.0,zSquared=0.0;
    for(const CorrespondencePair &pair:pairs){const QVector3D residual=result.matrix.map(pair.source)-pair.target;squared+=residual.lengthSquared();xySquared+=residual.x()*residual.x()+residual.y()*residual.y();zSquared+=residual.z()*residual.z();}
    result.uniqueReferences=unique.size();
    result.rmse=float(std::sqrt(squared/pairs.size()));
    result.xyRmse=float(std::sqrt(xySquared/pairs.size()));
    result.zRmse=float(std::sqrt(zSquared/pairs.size()));
    result.ok=true;
    return result;
}

QMatrix4x4 scaledRigidIncrement(const QMatrix4x4 &matrix, float factor) {
    const QQuaternion rotation=QQuaternion::fromRotationMatrix(matrix.normalMatrix()).normalized();
    const QQuaternion scaled=QQuaternion::slerp(QQuaternion(),rotation,factor).normalized();
    QMatrix4x4 result;result.setToIdentity();result.rotate(scaled);
    result(0,3)=matrix(0,3)*factor;result(1,3)=matrix(1,3)*factor;result(2,3)=matrix(2,3)*factor;
    return result;
}

void applyTransform(QVector<Point3D> *points, const QMatrix4x4 &matrix) {
    for(Point3D &point:*points){const QVector3D transformed=matrix.map(vectorOf(point));point.x=transformed.x();point.y=transformed.y();point.z=transformed.z();if(point.nx!=0||point.ny!=0||point.nz!=0){const QVector3D normal=matrix.mapVector(QVector3D(point.nx,point.ny,point.nz));point.nx=normal.x();point.ny=normal.y();point.nz=normal.z();}}
}

// Counts how many points of `points` land inside `bounds` after `matrix`,
// without materialising a transformed copy. The step-factor trials only need the
// count; the accepted factor is applied to the moving cloud once, so a whole
// cloud copy per trial factor disappears from the inner loop.
int countInsideBoundsTransformed(const QVector<Point3D> &points,
                                 const QMatrix4x4 &matrix,
                                 const Bounds &bounds,
                                 int minimum) {
    int count = 0;
    for (const Point3D &point : points) {
        if (insideBounds(matrix.map(vectorOf(point)), bounds) && ++count >= minimum) break;
    }
    return count;
}

void copyCloudPoints(QVector<Point3D> *destination, const QVector<Point3D> &source) {
    destination->resize(source.size());
    if (!source.isEmpty()) std::copy(source.cbegin(), source.cend(), destination->begin());
}

// Transforms the scratch buffer in place with `matrix`, counting how many
// points land inside `bounds`. Scanning stops as soon as the count reaches
// `minimum`: at that point the threshold is known to be met, so rejected
// step factors only pay for the points actually examined.
int countInsideBoundsAfterTransform(QVector<Point3D> *scratch,
                                    const QMatrix4x4 &matrix,
                                    const Bounds &bounds,
                                    int minimum) {
    int count = 0;
    const int size = scratch->size();
    for (int index = 0; index < size; ++index) {
        Point3D &point = (*scratch)[index];
        const QVector3D transformed = matrix.map(vectorOf(point));
        point.x = transformed.x(); point.y = transformed.y(); point.z = transformed.z();
        if (point.nx != 0 || point.ny != 0 || point.nz != 0) {
            const QVector3D normal = matrix.mapVector(QVector3D(point.nx, point.ny, point.nz));
            point.nx = normal.x(); point.ny = normal.y(); point.nz = normal.z();
        }
        if (insideBounds(transformed, bounds) && ++count >= minimum) break;
    }
    return count;
}

// XYZ-only variant for step-factor boundary trials: normals play no role in
// the overlap-box count, so the scratch transform skips them entirely.
int countInsideBoundsAfterTransformXyz(QVector<Point3D> *scratch,
                                       const QMatrix4x4 &matrix,
                                       const Bounds &bounds,
                                       int minimum) {
    int count = 0;
    const int size = scratch->size();
    for (int index = 0; index < size; ++index) {
        Point3D &point = (*scratch)[index];
        const QVector3D transformed = matrix.map(QVector3D(point.x, point.y, point.z));
        point.x = transformed.x(); point.y = transformed.y(); point.z = transformed.z();
        if (insideBounds(transformed, bounds) && ++count >= minimum) break;
    }
    return count;
}

float rotationAngle(const QMatrix4x4 &matrix) {
    const float cosine=qBound(-1.0f,(matrix(0,0)+matrix(1,1)+matrix(2,2)-1.0f)*0.5f,1.0f);
    return qRadiansToDegrees(std::acos(cosine));
}

float translationLength(const QMatrix4x4 &matrix) {
    return QVector3D(matrix(0,3),matrix(1,3),matrix(2,3)).length();
}

QPair<float,float> projectionRange(const QVector<Point3D> &points, const QVector3D &axis) {
    float minimum=std::numeric_limits<float>::max(),maximum=-minimum;
    for(const Point3D &point:points){const float value=QVector3D::dotProduct(vectorOf(point),axis);minimum=qMin(minimum,value);maximum=qMax(maximum,value);}
    return {minimum,maximum};
}

struct PlaneDiagnostic {
    bool valid=false;
    QVector3D normal,centroid;
    float offset=0,height=0;
    int inlierCount=0;
    float inlierRatio=0.0f;
    float rms=0.0f;
    float tiltDegrees=0.0f;
    QString method;
    QString rejectionReason;
    QVector3D axisU, axisV;
};
struct PlanePeak { float height=0; int count=0; };
struct DistanceDiagnostic {
    int total=0,matched=0;
    // Raw hit counts. The ratios below keep the legacy denominators (every
    // sampled point) while the *OfMatched helpers divide by the points that
    // actually found a counterpart, which is the only meaningful denominator
    // when the two views observe partly different surface.
    int within3Count=0,within5Count=0,within10Count=0;
    float p50=0,p90=0,p95=0,within3=0,within5=0,within10=0;
    float matchedRatio() const { return total>0 ? float(matched)/float(total) : 0.0f; }
    float within3OfMatched() const { return matched>0 ? float(within3Count)/float(matched) : 0.0f; }
    float within5OfMatched() const { return matched>0 ? float(within5Count)/float(matched) : 0.0f; }
};

QVector3D choosePlaneAxisU(const QVector3D &normal, const QVector3D &preferred)
{
    QVector3D axis = preferred - normal * QVector3D::dotProduct(preferred, normal);
    if (axis.lengthSquared() < 1.0e-8f) {
        axis = QVector3D(1, 0, 0) - normal * normal.x();
        if (axis.lengthSquared() < 1.0e-8f)
            axis = QVector3D(0, 1, 0) - normal * normal.y();
    }
    if (axis.lengthSquared() < 1.0e-8f) axis = QVector3D(1, 0, 0);
    axis.normalize();
    return axis;
}

void assignPlaneAxes(PlaneDiagnostic *plane, const QVector3D &preferred)
{
    if (!plane || !plane->valid) return;
    plane->axisU = choosePlaneAxisU(plane->normal, preferred);
    plane->axisV = QVector3D::crossProduct(plane->normal, plane->axisU).normalized();
}

Bounds projectedBounds(const QVector<Point3D> &points,
                       const QVector3D &origin,
                       const QVector3D &axisU,
                       const QVector3D &axisV,
                       const QVector3D &axisN)
{
    Bounds bounds;
    bounds.oriented = true;
    bounds.origin = origin;
    bounds.axisU = axisU;
    bounds.axisV = axisV;
    bounds.axisN = axisN;
    for (const Point3D &point : points) {
        if (!finitePoint(point)) continue;
        const QVector3D delta = vectorOf(point) - origin;
        const QVector3D local(QVector3D::dotProduct(delta, axisU),
                              QVector3D::dotProduct(delta, axisV),
                              QVector3D::dotProduct(delta, axisN));
        bounds.minimum.setX(qMin(bounds.minimum.x(), local.x()));
        bounds.minimum.setY(qMin(bounds.minimum.y(), local.y()));
        bounds.minimum.setZ(qMin(bounds.minimum.z(), local.z()));
        bounds.maximum.setX(qMax(bounds.maximum.x(), local.x()));
        bounds.maximum.setY(qMax(bounds.maximum.y(), local.y()));
        bounds.maximum.setZ(qMax(bounds.maximum.z(), local.z()));
        bounds.valid = true;
    }
    return bounds;
}

Bounds intersectBounds(const Bounds &a, const Bounds &b)
{
    Bounds result = a;
    if (!a.valid || !b.valid) { result.valid = false; return result; }
    result.minimum.setX(qMax(a.minimum.x(), b.minimum.x()));
    result.minimum.setY(qMax(a.minimum.y(), b.minimum.y()));
    result.minimum.setZ(qMax(a.minimum.z(), b.minimum.z()));
    result.maximum.setX(qMin(a.maximum.x(), b.maximum.x()));
    result.maximum.setY(qMin(a.maximum.y(), b.maximum.y()));
    result.maximum.setZ(qMin(a.maximum.z(), b.maximum.z()));
    result.valid = result.maximum.x() > result.minimum.x()
        && result.maximum.y() > result.minimum.y()
        && result.maximum.z() > result.minimum.z();
    return result;
}

DistanceDiagnostic nearestDistanceDiagnostic(const QVector<Point3D> &source,
                                             const QVector<Point3D> &target) {
    DistanceDiagnostic result;
    const QVector<Point3D> moving=voxelDownsample(source,2.0f),reference=voxelDownsample(target,2.0f);
    result.total=moving.size();
    if(moving.isEmpty()||reference.isEmpty())return result;
    constexpr float searchDistance=15.0f;const SpatialIndex index=buildIndex(reference,searchDistance);const float limit2=searchDistance*searchDistance;
    QVector<float> distances;distances.reserve(moving.size());int within3=0,within5=0,within10=0;
    for(const Point3D &point:moving){const GridKey key=gridKey(point,searchDistance);float best=limit2;bool found=false;
        for(qint64 z=-1;z<=1;++z)for(qint64 y=-1;y<=1;++y)for(qint64 x=-1;x<=1;++x){const auto cell=index.cells.find({key.x+x,key.y+y,key.z+z});if(cell==index.cells.end())continue;for(int candidate:cell->second){const float value=(vectorOf(point)-vectorOf(reference[candidate])).lengthSquared();if(value<best){best=value;found=true;}}}
        if(!found)continue;const float distance=std::sqrt(best);distances.push_back(distance);within3+=distance<=3.0f;within5+=distance<=5.0f;within10+=distance<=10.0f;}
    result.matched=distances.size();
    result.within3Count=within3;result.within5Count=within5;result.within10Count=within10;
    if(distances.isEmpty())return result;
    std::sort(distances.begin(),distances.end());const auto percentile=[&](float p){return distances[qBound(0,int(std::floor(p*float(distances.size()-1))),int(distances.size()-1))];};
    result.p50=percentile(.50f);result.p90=percentile(.90f);result.p95=percentile(.95f);const float count=float(moving.size());result.within3=within3/count;result.within5=within5/count;result.within10=within10/count;return result;
}

QVector<Point3D> structuralPoints(const QVector<Point3D> &points,
                                  const PlaneDiagnostic &plane,
                                  float exclusionDistance,
                                  const Bounds &overlap) {
    QVector<Point3D> result;
    if(!plane.valid||!overlap.valid)return result;
    result.reserve(points.size()/4);
    for(const Point3D &point:points){
        const QVector3D value=vectorOf(point);
        if(!insideBounds(value,overlap))continue;
        if(std::abs(QVector3D::dotProduct(plane.normal,value)-plane.offset)<=exclusionDistance)continue;
        result.push_back(point);
    }
    return result;
}

QVector<Point3D> structureWeightedPoints(const QVector<Point3D> &points,
                                          const PlaneDiagnostic &plane,
                                          float exclusionDistance,
                                          int supportStride,
                                          bool includeLocalBoundary = false) {
    QVector<Point3D> result;
    if(!plane.valid)return result;
    result.reserve(points.size()/3);
    const Bounds localBounds = includeLocalBoundary
        ? projectedBounds(points, plane.centroid, plane.axisU, plane.axisV, plane.normal)
        : Bounds{};
    int supportIndex=0;
    for(const Point3D &point:points){
        const QVector3D value = vectorOf(point);
        const bool structural=std::abs(QVector3D::dotProduct(plane.normal,value)-plane.offset)>exclusionDistance;
        bool boundary = false;
        if (includeLocalBoundary && localBounds.valid && !structural) {
            const QVector3D delta = value-plane.centroid;
            const float u = QVector3D::dotProduct(delta, plane.axisU);
            const float v = QVector3D::dotProduct(delta, plane.axisV);
            constexpr float edgeWidthMm = 2.0f;
            boundary = u-localBounds.minimum.x()<=edgeWidthMm
                || localBounds.maximum.x()-u<=edgeWidthMm
                || v-localBounds.minimum.y()<=edgeWidthMm
                || localBounds.maximum.y()-v<=edgeWidthMm;
        }
        if(structural||boundary||((supportIndex++%qMax(1,supportStride))==0))
            result.push_back(point);
    }
    return result;
}

QVector<Point3D> pureStructuralPoints(const QVector<Point3D> &points,
                                      const PlaneDiagnostic &plane,
                                      float exclusionDistance) {
    QVector<Point3D> result;
    if(!plane.valid)return result;
    result.reserve(points.size()/4);
    for(const Point3D &point:points)
        if(std::abs(QVector3D::dotProduct(plane.normal,vectorOf(point))-plane.offset)>exclusionDistance)
            result.push_back(point);
    return result;
}

float translatedCoverage(const QVector<Point3D> &source,
                         const QVector<Point3D> &target,
                         const SpatialIndex &targetIndex,
                         const SpatialIndex &sourceIndex,
                         const QVector3D &translation,
                         float distance) {
    if(source.isEmpty()||target.isEmpty())return 0.0f;
    const float limit2=distance*distance;
    const auto covered=[&](const Point3D &point,const QVector3D &offset,
                           const QVector<Point3D> &reference,const SpatialIndex &index){
        Point3D moved=point;moved.x+=offset.x();moved.y+=offset.y();moved.z+=offset.z();
        const GridKey key=gridKey(moved,distance);
        for(qint64 z=-1;z<=1;++z)for(qint64 y=-1;y<=1;++y)for(qint64 x=-1;x<=1;++x){
            const auto cell=index.cells.find({key.x+x,key.y+y,key.z+z});
            if(cell==index.cells.end())continue;
            for(int candidate:cell->second)
                if((vectorOf(moved)-vectorOf(reference[candidate])).lengthSquared()<=limit2)return true;
        }
        return false;
    };
    int forward=0,backward=0;
    for(const Point3D &point:source)forward+=covered(point,translation,target,targetIndex);
    for(const Point3D &point:target)backward+=covered(point,-translation,source,sourceIndex);
    return qMin(float(forward)/float(source.size()),float(backward)/float(target.size()));
}

// Trimmed nearest-neighbour objective for the coarse in-plane search.
//
// The bidirectional coverage score above is diluted by the points the other view
// never observed - exactly the regime the coarse search has to work in. On the
// 5_40 pair the best translation moved that score from 0.150 to 0.166, i.e.
// below the legacy 5% acceptance gate, even though the surface was really
// misplaced by about 8 mm. This score averages the K smallest nearest-neighbour
// distances after translating the source (smaller is better); a point without a
// neighbour inside searchDistance contributes that distance, so a candidate can
// never win by matching fewer points.
float trimmedNearestScore(const QVector<Point3D> &source,
                          const QVector<Point3D> &target,
                          const SpatialIndex &targetIndex,
                          const QVector3D &translation,
                          float searchDistance,
                          int inlierCount)
{
    if (source.isEmpty() || inlierCount <= 0) return std::numeric_limits<float>::max();
    const float limitSquared = searchDistance * searchDistance;
    // A neighbour much closer than the search radius already is the smallest
    // term of the sum by a wide margin, so the remaining cells cannot change the
    // ranking and the scan stops early.
    const float earlyExitSquared = limitSquared * 0.0625f;
    const std::array<GridKey, 27> &offsets = neighborOffsets27();
    QVector<float> distances;
    distances.reserve(source.size());
    for (const Point3D &point : source) {
        Point3D moved = point;
        moved.x += translation.x(); moved.y += translation.y(); moved.z += translation.z();
        const QVector3D position = vectorOf(moved);
        const GridKey key = gridKey(moved, searchDistance);
        float best = limitSquared;
        for (const GridKey &offset : offsets) {
            const auto cell = targetIndex.cells.find({key.x + offset.x, key.y + offset.y, key.z + offset.z});
            if (cell == targetIndex.cells.end()) continue;
            for (int candidate : cell->second) {
                const float distance = (position - vectorOf(target[candidate])).lengthSquared();
                if (distance < best) best = distance;
            }
            if (best <= earlyExitSquared) break;
        }
        distances.push_back(best < limitSquared ? std::sqrt(best) : searchDistance);
    }
    if (distances.isEmpty()) return std::numeric_limits<float>::max();
    const int keep = qBound(1, inlierCount, int(distances.size()));
    std::nth_element(distances.begin(), distances.begin() + (keep - 1), distances.end());
    float sum = 0.0f;
    for (int index = 0; index < keep; ++index) sum += distances[index];
    return sum / float(keep);
}

void runWideStructureDiagnostic(const QVector<Point3D> &source,
                                const QVector<Point3D> &target,
                                const PlaneDiagnostic &targetPlane,
                                const IcpOptions &options,
                                IcpDiagnostics *diagnostic,
                                QMatrix4x4 *candidateMatrix=nullptr) {
    diagnostic->wideStructureDiagnosticAttempted=true;
    QVector<Point3D> moving=voxelDownsample(source,4.0f),reference=voxelDownsample(target,4.0f);
    if(moving.size()<options.minimumStructuralPoints||reference.size()<options.minimumStructuralPoints||!targetPlane.valid){
        diagnostic->wideStructureDiagnosticReason=QStringLiteral("大范围诊断结构样本不足：%1/%2").arg(moving.size()).arg(reference.size());return;
    }
    const float scoreDistance=qMax(3.0f,options.tangentCoarseScoreDistanceMm);
    const SpatialIndex targetIndex=buildIndex(reference,scoreDistance),sourceIndex=buildIndex(moving,scoreDistance);
    QVector3D axisU = targetPlane.axisU.lengthSquared() > 1.0e-8f
        ? targetPlane.axisU : choosePlaneAxisU(targetPlane.normal, QVector3D(1,0,0));
    axisU.normalize();const QVector3D axisV=QVector3D::crossProduct(targetPlane.normal,axisU).normalized();
    const int trimmedCount=qBound(1,
        int(std::ceil(double(options.tangentCoarseTrimmedRatio)*double(moving.size()))),
        int(moving.size()));
    const auto trimmedScore=[&](const QVector<Point3D> &cloud,const QVector3D &translation){
        return trimmedNearestScore(cloud,reference,targetIndex,translation,scoreDistance,trimmedCount);
    };
    // Same objective switch as the tangent search: the legacy coverage score is
    // negated so both modes minimise. wideStructureCoverageBefore/Best keep
    // reporting plain coverage so historical comparisons remain possible.
    const auto objective=[&](const QVector<Point3D> &cloud,const QVector3D &translation){
        return options.tangentCoarseTrimmedScore
            ?trimmedScore(cloud,translation)
            :-translatedCoverage(cloud,reference,targetIndex,sourceIndex,translation,scoreDistance);
    };
    QVector3D best;float bestScore=objective(moving,{});
    diagnostic->wideStructureCoverageBefore=translatedCoverage(moving,reference,targetIndex,sourceIndex,{},scoreDistance);
    const auto search=[&](float centerU,float centerV,float radius,float step){
        for(float u=centerU-radius;u<=centerU+radius+.001f;u+=step)
            for(float v=centerV-radius;v<=centerV+radius+.001f;v+=step){
                const QVector3D candidate=axisU*u+axisV*v;
                if(candidate.length()>options.wideStructureSearchRadiusMm+.001f)continue;
                const float score=objective(moving,candidate);
                if(score<bestScore){bestScore=score;best=candidate;}
            }
    };
    search(0,0,options.wideStructureSearchRadiusMm,options.wideStructureSearchStepMm);
    const float centerU=QVector3D::dotProduct(best,axisU),centerV=QVector3D::dotProduct(best,axisV);
    search(centerU,centerV,options.wideStructureSearchStepMm,options.wideStructureRefineStepMm);
    QVector3D centroid;for(const Point3D &point:moving)centroid+=vectorOf(point);centroid/=float(moving.size());
    QVector<Point3D> translated=moving;for(Point3D &point:translated){point.x+=best.x();point.y+=best.y();point.z+=best.z();}
    float bestYaw=0;
    for(float angle=-options.wideStructureYawRadiusDegrees;angle<=options.wideStructureYawRadiusDegrees+.001f;angle+=options.wideStructureYawStepDegrees){
        QMatrix4x4 rotation;rotation.setToIdentity();rotation.translate(centroid+best);rotation.rotate(angle,targetPlane.normal);rotation.translate(-(centroid+best));
        QVector<Point3D> rotated=translated;applyTransform(&rotated,rotation);
        const float score=objective(rotated,{});
        if(score<bestScore){bestScore=score;bestYaw=angle;}
    }
    diagnostic->wideStructureDiagnosticValid=true;
    diagnostic->wideStructureCoverageBest=translatedCoverage(moving,reference,targetIndex,sourceIndex,best,scoreDistance);
    diagnostic->wideStructureX=best.x();diagnostic->wideStructureY=best.y();diagnostic->wideStructureZ=best.z();
    diagnostic->wideStructureYawDegrees=bestYaw;
    if(candidateMatrix){
        QMatrix4x4 translation;translation.setToIdentity();translation.translate(best);
        QMatrix4x4 rotation;rotation.setToIdentity();
        if(std::abs(bestYaw)>1.0e-6f){
            rotation.translate(centroid+best);rotation.rotate(bestYaw,targetPlane.normal);rotation.translate(-(centroid+best));
        }
        *candidateMatrix=rotation*translation;
    }
    diagnostic->wideStructureDiagnosticReason=(options.applyWideStructureCandidate
        ?QStringLiteral("大范围结构候选：5 mm 双向覆盖率由 %1 提升至 %2，作为 ICP 初值")
        :QStringLiteral("只读诊断：5 mm 双向覆盖率由 %1 提升至 %2，候选不应用"))
        .arg(diagnostic->wideStructureCoverageBefore,0,'f',4).arg(bestScore,0,'f',4);
}

QVector3D tangentCoarseTranslation(const QVector<Point3D> &source,
                                   const QVector<Point3D> &target,
                                   const PlaneDiagnostic &targetPlane,
                                   const IcpOptions &options,
                                   IcpDiagnostics *diagnostic,
                                   QMatrix4x4 *coarseRotation) {
    diagnostic->tangentCoarseAlignmentAttempted=true;
    // Reported even when the search bails out below, so a skipped coarse stage
    // always states which objective it would have used.
    diagnostic->tangentCoarseScoreMode=options.tangentCoarseTrimmedScore
        ?QStringLiteral("trimmed_nearest"):QStringLiteral("legacy_coverage");
    if(source.size()<options.minimumStructuralPoints||target.size()<options.minimumStructuralPoints||!targetPlane.valid){
        diagnostic->tangentCoarseAlignmentReason=QStringLiteral("结构点或目标平面不足，跳过切向粗对齐");return {};
    }
    const QVector<Point3D> moving=voxelDownsample(source,4.0f),reference=voxelDownsample(target,4.0f);
    if(moving.size()<options.minimumStructuralPoints||reference.size()<options.minimumStructuralPoints){
        diagnostic->tangentCoarseAlignmentReason=QStringLiteral("4 mm 粗搜索样本不足，跳过切向粗对齐");return {};
    }
    const float scoreDistance=qMax(3.0f,options.tangentCoarseScoreDistanceMm);
    const SpatialIndex targetIndex=buildIndex(reference,scoreDistance),sourceIndex=buildIndex(moving,scoreDistance);
    QVector3D axisU = targetPlane.axisU.lengthSquared() > 1.0e-8f
        ? targetPlane.axisU : choosePlaneAxisU(targetPlane.normal, QVector3D(1,0,0));
    axisU.normalize();const QVector3D axisV=QVector3D::crossProduct(targetPlane.normal,axisU).normalized();
    coarseRotation->setToIdentity();
    const int trimmedCount=qBound(1,
        int(std::ceil(double(options.tangentCoarseTrimmedRatio)*double(moving.size()))),
        int(moving.size()));
    const auto trimmedScore=[&](const QVector3D &translation){
        return trimmedNearestScore(moving,reference,targetIndex,translation,scoreDistance,trimmedCount);
    };
    const auto coverageScore=[&](const QVector3D &translation){
        return translatedCoverage(moving,reference,targetIndex,sourceIndex,translation,scoreDistance);
    };
    // Lower is better in both modes, so the legacy coverage score is negated and
    // the search below stays identical for both objectives.
    const auto objective=[&](const QVector3D &translation){
        return options.tangentCoarseTrimmedScore?trimmedScore(translation):-coverageScore(translation);
    };
    diagnostic->tangentCoverageBefore=coverageScore({});
    diagnostic->tangentCoarseScoreBefore=trimmedScore({});
    QVector3D best;float bestScore=objective(best);
    const auto search=[&](float centerU,float centerV,float radius,float step){
        for(float u=centerU-radius;u<=centerU+radius+.001f;u+=step)
            for(float v=centerV-radius;v<=centerV+radius+.001f;v+=step){
                const QVector3D candidate=axisU*u+axisV*v;
                if(candidate.length()>options.tangentCoarseSearchRadiusMm+.001f)continue;
                const float value=objective(candidate);
                if(value<bestScore){bestScore=value;best=candidate;}
            }
    };
    search(0,0,options.tangentCoarseSearchRadiusMm,options.tangentCoarseStepMm);
    QVector3D centroid;for(const Point3D &point:moving)centroid+=vectorOf(point);centroid/=float(moving.size());
    float bestYaw=0.0f;
    QVector<Point3D> translatedMoving=moving;for(Point3D &point:translatedMoving){point.x+=best.x();point.y+=best.y();point.z+=best.z();}
    for(float angle=-options.tangentCoarseYawRadiusDegrees;angle<=options.tangentCoarseYawRadiusDegrees+.001f;angle+=options.tangentCoarseYawStepDegrees){
        QMatrix4x4 rotation;rotation.setToIdentity();rotation.translate(centroid+best);rotation.rotate(angle,targetPlane.normal);rotation.translate(-(centroid+best));
        QVector<Point3D> rotated=translatedMoving;applyTransform(&rotated,rotation);
        float value=0.0f;
        if(options.tangentCoarseTrimmedScore){
            value=trimmedNearestScore(rotated,reference,targetIndex,{},scoreDistance,trimmedCount);
        }else{
            const SpatialIndex rotatedIndex=buildIndex(rotated,scoreDistance);
            value=-translatedCoverage(rotated,reference,targetIndex,rotatedIndex,{},scoreDistance);
        }
        if(value<bestScore){bestScore=value;bestYaw=angle;*coarseRotation=rotation;}
    }
    diagnostic->tangentCoarseYawDegrees=bestYaw;
    const float centerU=QVector3D::dotProduct(best,axisU),centerV=QVector3D::dotProduct(best,axisV);
    search(centerU,centerV,options.tangentCoarseStepMm,options.tangentCoarseRefineStepMm);
    const float coverageAfter=coverageScore(best);
    const float trimmedAfter=options.tangentCoarseTrimmedScore?bestScore:trimmedScore(best);
    diagnostic->tangentCoverageAfter=coverageAfter;
    diagnostic->tangentCoarseScoreAfter=trimmedAfter;
    diagnostic->tangentCoarseX=best.x();diagnostic->tangentCoarseY=best.y();diagnostic->tangentCoarseZ=best.z();
    const float coverageGain=coverageAfter-diagnostic->tangentCoverageBefore;
    const float scoreGain=diagnostic->tangentCoarseScoreBefore>1.0e-6f
        ?(diagnostic->tangentCoarseScoreBefore-trimmedAfter)/diagnostic->tangentCoarseScoreBefore
        :0.0f;
    const bool accepted=options.tangentCoarseTrimmedScore
        ?scoreGain>=options.tangentCoarseMinimumScoreGain
        :coverageGain>=options.tangentCoarseMinimumCoverageGain;
    if(!accepted){
        diagnostic->tangentCoarseAlignmentReason=options.tangentCoarseTrimmedScore
            ?QStringLiteral("切向搜索 trimmed 目标改善 %1 低于 %2，保持机器人初值")
                .arg(QString::number(double(scoreGain),'f',4))
                .arg(QString::number(double(options.tangentCoarseMinimumScoreGain),'g',4))
            :QStringLiteral("切向搜索覆盖率增益 %1 低于 %2，保持机器人初值")
                .arg(QString::number(double(coverageGain),'f',4))
                .arg(QString::number(double(options.tangentCoarseMinimumCoverageGain),'g',4));
        return {};
    }
    diagnostic->tangentCoarseAlignmentApplied=true;
    diagnostic->tangentCoarseAlignmentReason=options.tangentCoarseTrimmedScore
        ?QStringLiteral("切向结构对齐：trimmed 目标 %1 降至 %2，5 mm 双向覆盖率 %3 → %4")
            .arg(QString::number(double(diagnostic->tangentCoarseScoreBefore),'f',4))
            .arg(QString::number(double(trimmedAfter),'f',4))
            .arg(QString::number(double(diagnostic->tangentCoverageBefore),'f',4))
            .arg(QString::number(double(coverageAfter),'f',4))
        :QStringLiteral("切向结构覆盖率由 %1 提升至 %2")
            .arg(QString::number(double(diagnostic->tangentCoverageBefore),'f',4))
            .arg(QString::number(double(coverageAfter),'f',4));
    return best;
}

PlaneDiagnostic pcaPlane(const QVector<Point3D> &points,
                         float preferredHeight=std::numeric_limits<float>::quiet_NaN(),
                         const IcpOptions *fitOptions=nullptr) {
    PlaneDiagnostic result;
    const QVector<Point3D> sample=voxelDownsample(points,1.0f);
    if(sample.size()<100)return result;
    const float inlierDistance=fitOptions?qMax(.2f,fitOptions->planeFitInlierDistanceMm):1.5f;
    const int iterations=fitOptions?qMax(0,fitOptions->planeFitIterations):0;
    const float minimumRatio=fitOptions?qBound(0.01f,fitOptions->minimumPlaneInlierRatio,1.0f):0.15f;
    const float maximumTilt=fitOptions?qMax(0.0f,fitOptions->maximumPlaneTiltDegrees):75.0f;
    const auto fit=[&](const QVector<Point3D> &fitPoints,QVector3D *fitMean,QVector3D *fitNormal){
        *fitMean={};double matrix[3][3]{};
        for(const Point3D &point:fitPoints)*fitMean+=vectorOf(point);
        *fitMean/=float(fitPoints.size());
        for(const Point3D &point:fitPoints){const QVector3D d=vectorOf(point)-*fitMean;const double v[3]{d.x(),d.y(),d.z()};for(int row=0;row<3;++row)for(int column=0;column<3;++column)matrix[row][column]+=v[row]*v[column];}
        smallestEigenvector(matrix,fitNormal);
    };
    const auto finalize=[&](QVector<Point3D> planePoints,const QString &method,float height)->PlaneDiagnostic {
        PlaneDiagnostic fitted;
        if(planePoints.size()<100)return fitted;
        QVector3D mean,normal;fit(planePoints,&mean,&normal);
        if(normal.lengthSquared()<=1.0e-8f)return fitted;
        if(normal.z()<0)normal=-normal;
        for(int iteration=0;iteration<iterations;++iteration){
            QVector<Point3D> inliers;inliers.reserve(planePoints.size());
            for(const Point3D &point:planePoints)if(std::abs(QVector3D::dotProduct(normal,vectorOf(point)-mean))<=inlierDistance)inliers.push_back(point);
            if(inliers.size()<100)break;
            fit(inliers,&mean,&normal);if(normal.z()<0)normal=-normal;planePoints.swap(inliers);
        }
        double squared=0.0;for(const Point3D &point:planePoints){const double d=QVector3D::dotProduct(normal,vectorOf(point)-mean);squared+=d*d;}
        fitted.normal=normal;fitted.centroid=mean;fitted.offset=QVector3D::dotProduct(mean,normal);fitted.height=height;
        fitted.inlierCount=planePoints.size();fitted.inlierRatio=float(planePoints.size())/float(sample.size());
        fitted.rms=planePoints.isEmpty()?0.0f:float(std::sqrt(squared/double(planePoints.size())));
        fitted.tiltDegrees=qRadiansToDegrees(std::acos(qBound(-1.0f,std::abs(normal.z()),1.0f)));
        fitted.method=method;
        if (!std::isfinite(fitted.rms)) fitted.rejectionReason=QStringLiteral("plane_fit_non_finite");
        else if (fitted.inlierRatio < minimumRatio) fitted.rejectionReason=QStringLiteral("plane_fit_inlier_ratio_low");
        else if (fitted.tiltDegrees > maximumTilt) fitted.rejectionReason=QStringLiteral("plane_tilt_exceeds_limit");
        fitted.valid=fitted.rejectionReason.isEmpty();
        return fitted;
    };

    // Compatibility path for shallow planes: retain the historic Z-height band
    // selection and only fall back when it cannot produce a valid shallow fit.
    constexpr float heightBin=0.5f;
    std::unordered_map<qint64,int> histogram;histogram.reserve(size_t(sample.size()/4));
    qint64 dominantBin=0;int dominantCount=0;
    for(const Point3D &point:sample){const qint64 bin=qint64(std::floor(point.z/heightBin));const int count=++histogram[bin];if(count>dominantCount){dominantCount=count;dominantBin=bin;}}
    if(std::isfinite(preferredHeight)){
        float bestDifference=std::numeric_limits<float>::max();const int minimumPeakCount=qMax(100,dominantCount/10);
        for(const auto &entry:histogram)if(entry.second>=minimumPeakCount){const float height=(float(entry.first)+.5f)*heightBin;const float difference=std::abs(height-preferredHeight);if(difference<bestDifference){bestDifference=difference;dominantBin=entry.first;}}
    }
    const float dominantHeight=(float(dominantBin)+.5f)*heightBin;
    const float heightBand=fitOptions?qMax(1.0f,fitOptions->planeFitHeightBandMm):1.5f;
    QVector<Point3D> planePoints;planePoints.reserve(dominantCount*4);
    for(const Point3D &point:sample)if(std::abs(point.z-dominantHeight)<=heightBand)planePoints.push_back(point);
    PlaneDiagnostic shallow=finalize(planePoints,QStringLiteral("z_height_band"),dominantHeight);
    const float shallowBandRatio = float(dominantCount) / float(sample.size());
    if(shallow.valid&&shallow.tiltDegrees<=36.9f&&shallowBandRatio>=0.35f)return shallow;

    // Deterministic voxel-sampled RANSAC for arbitrary plane orientation.
    PlaneDiagnostic best;const int hypotheses=fitOptions?qMax(1,fitOptions->planeFitRansacHypotheses):256;
    const int n=sample.size();quint32 state=0x9e3779b9u;
    for(int hypothesis=0;hypothesis<hypotheses;++hypothesis){
        state=state*1664525u+1013904223u;const int ia=int(state%quint32(n));
        state=state*1664525u+1013904223u;const int ib=int(state%quint32(n));
        state=state*1664525u+1013904223u;const int ic=int(state%quint32(n));
        if(ia==ib||ia==ic||ib==ic)continue;
        const QVector3D a=vectorOf(sample[ia]),b=vectorOf(sample[ib]),c=vectorOf(sample[ic]);
        QVector3D normal=QVector3D::crossProduct(b-a,c-a);if(normal.lengthSquared()<1.0e-8f)continue;normal.normalize();if(normal.z()<0)normal=-normal;
        QVector<Point3D> inliers;inliers.reserve(sample.size());const float offset=QVector3D::dotProduct(a,normal);
        for(const Point3D &point:sample)if(std::abs(QVector3D::dotProduct(vectorOf(point),normal)-offset)<=inlierDistance)inliers.push_back(point);
        PlaneDiagnostic candidate=finalize(inliers,QStringLiteral("ransac_pca"),offset);
        if(candidate.inlierCount>best.inlierCount||(candidate.inlierCount==best.inlierCount&&candidate.rms<best.rms))best=candidate;
    }
    if(!best.valid){
        if (best.inlierCount >= shallow.inlierCount && best.inlierCount > 0) return best;
        if(shallow.inlierCount>0){
            shallow.method=QStringLiteral("z_height_band_rejected");
            shallow.valid=false;
            if (shallow.rejectionReason.isEmpty()) shallow.rejectionReason=QStringLiteral("plane_fit_orientation_ambiguous");
            return shallow;
        }
        return best;
    }
    return best;
}

QVector<Point3D> cropCloud(const QVector<Point3D> &points, const Bounds &bounds) {
    QVector<Point3D> result;
    result.reserve(points.size());
    for (const Point3D &point : points)
        if (insideBounds(vectorOf(point), bounds)) result.push_back(point);
    return result;
}

QVector<PlanePeak> planePeakCandidates(const QVector<Point3D> &points) {
    const QVector<Point3D> sample=voxelDownsample(points,1.0f);
    QVector<PlanePeak> result;
    if(sample.size()<100)return result;
    constexpr float heightBin=.5f;
    std::unordered_map<qint64,int> histogram;
    int dominant=0;
    for(const Point3D &point:sample)dominant=qMax(dominant,++histogram[qint64(std::floor(point.z/heightBin))]);
    const int minimum=qMax(100,dominant/10);
    for(const auto &entry:histogram)if(entry.second>=minimum)
        result.push_back({(float(entry.first)+.5f)*heightBin,entry.second});
    std::sort(result.begin(),result.end(),[](const PlanePeak &a,const PlanePeak &b){return a.height<b.height;});
    return result;
}

// Chooses the local frame the overlap box must be built in, or an invalid
// plane when the legacy robot-XYZ intersection is the right one.
//
// The real steep fixtures show a shallow (~0.9 deg) support plane carrying the
// whole steep workpiece inside the crop. Routing only by the main plane's tilt
// then keeps the fixed robot-XYZ box, which reaches far beyond the surface the
// other view ever observed. When the dominant structure sitting on the main
// plane points elsewhere, that structure defines the crop frame instead.
PlaneDiagnostic structuralOverlapPlane(const QVector<Point3D> &targetCrop,
                                       const PlaneDiagnostic &sourcePlane,
                                       const PlaneDiagnostic &targetPlane,
                                       const IcpOptions &options)
{
    if(!targetPlane.valid)return {};
    if(targetPlane.tiltDegrees>36.9f
       ||(sourcePlane.valid&&sourcePlane.tiltDegrees>36.9f))return targetPlane;
    const Bounds cropBounds=cloudBounds(targetCrop);
    if(!cropBounds.valid)return {};
    const QVector<Point3D> structural=structuralPoints(targetCrop,targetPlane,
        options.structuralPlaneExclusionMm,cropBounds);
    if(structural.size()<options.minimumStructuralPoints)return {};
    const PlaneDiagnostic structuralPlane=pcaPlane(structural,
        std::numeric_limits<float>::quiet_NaN(),&options);
    if(!structuralPlane.valid)return {};
    const float minimumCosine=float(std::cos(double(qDegreesToRadians(
        qBound(0.0f,options.structuralFrameRouteDegrees,90.0f)))));
    if(std::abs(QVector3D::dotProduct(structuralPlane.normal,targetPlane.normal))>=minimumCosine)return {};
    return structuralPlane;
}

bool trackPlaneIdentity(const QVector<ConvertedCloud> &clouds,
                        const IcpOptions &options,
                        QVector<float> *trackedHeights,
                        float *consensusHeight,
                        QString *reason) {
    trackedHeights->clear();
    if(!options.planeIdentityTrackingEnabled){if(reason)*reason=QStringLiteral("跨帧主平面身份跟踪已禁用");return false;}
    QVector<QVector<PlanePeak>> candidates;candidates.reserve(clouds.size());
    for(const ConvertedCloud &cloud:clouds)candidates.push_back(planePeakCandidates(cloud.sample));
    int bestCoverage=0,bestCount=-1;float bestSpread=std::numeric_limits<float>::max(),bestAnchor=0;
    for(const QVector<PlanePeak> &anchors:candidates)for(const PlanePeak &anchor:anchors){
        int coverage=0,totalCount=0;float spread=0;
        for(const QVector<PlanePeak> &frame:candidates){
            float nearest=std::numeric_limits<float>::max();int count=0;
            for(const PlanePeak &peak:frame){const float distance=std::abs(peak.height-anchor.height);if(distance<nearest){nearest=distance;count=peak.count;}}
            if(nearest<=options.planeIdentityHeightToleranceMm){++coverage;spread+=nearest;totalCount+=count;}
        }
        if(coverage>bestCoverage||(coverage==bestCoverage&&spread<bestSpread-1.0e-6f)
            ||(coverage==bestCoverage&&std::abs(spread-bestSpread)<=1.0e-6f&&totalCount>bestCount)){
            bestCoverage=coverage;bestSpread=spread;bestCount=totalCount;bestAnchor=anchor.height;
        }
    }
    if(bestCoverage!=clouds.size()){
        if(reason)*reason=QStringLiteral("候选主平面只能连续覆盖 %1/%2 帧，回退相邻匹配").arg(bestCoverage).arg(clouds.size());return false;
    }
    for(const QVector<PlanePeak> &frame:candidates){
        float selected=bestAnchor,nearest=std::numeric_limits<float>::max();
        for(const PlanePeak &peak:frame){const float distance=std::abs(peak.height-bestAnchor);if(distance<nearest){nearest=distance;selected=peak.height;}}
        trackedHeights->push_back(selected);
    }
    *consensusHeight=bestAnchor;
    if(reason)*reason=QStringLiteral("同一主平面连续覆盖 %1 帧，高度轨迹：%2")
        .arg(clouds.size()).arg([&](){QStringList values;for(float value:*trackedHeights)values<<QString::number(value,'f',2);return values.join(QStringLiteral(", "));}());
    return true;
}

bool trackSteepPlaneIdentity(const QVector<ConvertedCloud> &clouds,
                             const IcpOptions &options,
                             QString *reason)
{
    if (!options.planeIdentityTrackingEnabled || clouds.isEmpty()) return false;
    QVector<PlaneDiagnostic> planes;
    planes.reserve(clouds.size());
    for (const ConvertedCloud &cloud : clouds)
        planes.push_back(pcaPlane(cloud.sample, std::numeric_limits<float>::quiet_NaN(), &options));
    const PlaneDiagnostic &anchor = planes.first();
    if (!anchor.valid || anchor.tiltDegrees <= 36.9f) return false;
    for (int index = 1; index < planes.size(); ++index) {
        if (!planes[index].valid) return false;
        const float normalDifference = qRadiansToDegrees(std::acos(qBound(-1.0f,
            std::abs(QVector3D::dotProduct(anchor.normal, planes[index].normal)), 1.0f)));
        const float offsetDifference = std::abs(anchor.offset - planes[index].offset);
        if (normalDifference > options.planeIdentityNormalToleranceDegrees
            || offsetDifference > options.planeIdentityOffsetToleranceMm) {
            if (reason) *reason = QStringLiteral("陡峭主平面身份不一致：normal=%1 deg, offset=%2 mm")
                .arg(normalDifference,0,'f',3).arg(offsetDifference,0,'f',3);
            return false;
        }
    }
    if (reason) *reason = QStringLiteral("陡峭主平面按法向/平面偏移连续匹配");
    return true;
}

struct PairInitialData {
    int sourceCropCount = 0;
    int targetCropCount = 0;
    float projectedOverlapWidth = 0.0f;
    DistanceDiagnostic distances;
    PlaneDiagnostic sourcePlane;
    PlaneDiagnostic targetPlane;
};

PairInitialData computePairInitialData(const QVector<Point3D> &sourceCrop,
                                       const QVector<Point3D> &targetCrop,
                                       const QVector3D &stitchAxis,
                                       const IcpOptions &options,
                                       float preferredSourceHeight,
                                       float preferredTargetHeight,
                                       bool planeIdentityTracked) {
    PairInitialData data;
    data.sourceCropCount = int(sourceCrop.size());
    data.targetCropCount = int(targetCrop.size());
    if(!sourceCrop.isEmpty()&&!targetCrop.isEmpty()&&stitchAxis.lengthSquared()>1.0e-8f){
        const QVector3D axis=stitchAxis.normalized();const auto a=projectionRange(sourceCrop,axis),b=projectionRange(targetCrop,axis);
        data.projectedOverlapWidth=qMax(0.0f,qMin(a.second,b.second)-qMax(a.first,b.first));
    }
    data.distances=nearestDistanceDiagnostic(sourceCrop,targetCrop);
    data.sourcePlane=planeIdentityTracked?pcaPlane(sourceCrop,preferredSourceHeight,&options):pcaPlane(sourceCrop,std::numeric_limits<float>::quiet_NaN(),&options);
    data.targetPlane=planeIdentityTracked?pcaPlane(targetCrop,preferredTargetHeight,&options)
        :(data.sourcePlane.valid?pcaPlane(targetCrop,data.sourcePlane.height,&options):pcaPlane(targetCrop,std::numeric_limits<float>::quiet_NaN(),&options));
    if(data.sourcePlane.valid&&data.targetPlane.valid&&QVector3D::dotProduct(data.sourcePlane.normal,data.targetPlane.normal)<0){
        data.targetPlane.normal=-data.targetPlane.normal;data.targetPlane.offset=-data.targetPlane.offset;
    }
    if (data.targetPlane.valid) assignPlaneAxes(&data.targetPlane, stitchAxis);
    if (data.sourcePlane.valid) {
        const QVector3D preferred = data.targetPlane.valid ? data.targetPlane.axisU : stitchAxis;
        assignPlaneAxes(&data.sourcePlane, preferred);
    }
    return data;
}

void stampPairInitialData(IcpDiagnostics *diagnostic,
                          const PairInitialData &data,
                          bool planeIdentityTracked,
                          float preferredSourceHeight,
                          float preferredTargetHeight,
                          const QString &planeIdentityReason) {
    if(!diagnostic)return;
    diagnostic->sourceCropCount=data.sourceCropCount;diagnostic->targetCropCount=data.targetCropCount;
    diagnostic->projectedOverlapWidth=data.projectedOverlapWidth;
    const DistanceDiagnostic &distances=data.distances;
    diagnostic->nearestDistanceP50=distances.p50;diagnostic->nearestDistanceP90=distances.p90;diagnostic->nearestDistanceP95=distances.p95;diagnostic->coverageWithin3mm=distances.within3;diagnostic->coverageWithin5mm=distances.within5;diagnostic->coverageWithin10mm=distances.within10;
    // Use robust overlap statistics as the retry scale, never a single point or AABB edge.
    diagnostic->initialTranslation=distances.p90;
    diagnostic->planeIdentityTracked=planeIdentityTracked;
    diagnostic->trackedSourcePlaneHeight=preferredSourceHeight;
    diagnostic->trackedTargetPlaneHeight=preferredTargetHeight;
    diagnostic->planeIdentityReason=planeIdentityReason;
    if (!data.sourcePlane.valid && !data.sourcePlane.rejectionReason.isEmpty())
        diagnostic->steepPlaneRejectionReason = data.sourcePlane.rejectionReason;
    if (!data.targetPlane.valid && !data.targetPlane.rejectionReason.isEmpty())
        diagnostic->steepPlaneRejectionReason = data.targetPlane.rejectionReason;
    if(data.sourcePlane.valid&&data.targetPlane.valid){
        diagnostic->planeDiagnosticValid=true;
        diagnostic->planeNormalAngleDegrees=qRadiansToDegrees(std::acos(qBound(-1.0f,QVector3D::dotProduct(data.sourcePlane.normal,data.targetPlane.normal),1.0f)));
        diagnostic->initialAngleDegrees=diagnostic->planeNormalAngleDegrees;
        diagnostic->sourcePlaneHeight=data.sourcePlane.height;
        diagnostic->targetPlaneHeight=data.targetPlane.height;
        diagnostic->planeOffsetDifference=data.sourcePlane.height-data.targetPlane.height;
        diagnostic->planeFitMethod=data.targetPlane.method;
        diagnostic->sourcePlaneNormal=data.sourcePlane.normal;
        diagnostic->targetPlaneNormal=data.targetPlane.normal;
        diagnostic->sourcePlaneTiltDegrees=data.sourcePlane.tiltDegrees;
        diagnostic->targetPlaneTiltDegrees=data.targetPlane.tiltDegrees;
        diagnostic->sourcePlaneFitRms=data.sourcePlane.rms;
        diagnostic->targetPlaneFitRms=data.targetPlane.rms;
        diagnostic->sourcePlaneInlierCount=data.sourcePlane.inlierCount;
        diagnostic->targetPlaneInlierCount=data.targetPlane.inlierCount;
        diagnostic->sourcePlaneInlierRatio=data.sourcePlane.inlierRatio;
        diagnostic->targetPlaneInlierRatio=data.targetPlane.inlierRatio;
        diagnostic->steepPlaneDetected=qMax(data.sourcePlane.tiltDegrees,data.targetPlane.tiltDegrees)>36.9f;
        diagnostic->steepPlaneObservableDof=diagnostic->observableDof;
        diagnostic->planeIdentityNormalDifferenceDegrees=diagnostic->planeNormalAngleDegrees;
        diagnostic->planeIdentityOffsetDifferenceMm=std::abs(data.sourcePlane.offset-data.targetPlane.offset);
    }
}

// Attempt-invariant preprocessing shared between the strict attempt and its
// relaxed retry: bounding boxes, overlap crops, plane fits, initial distance
// statistics and the per-level source downsample base.
struct PairPreprocessCache {
    struct LevelTargetEntry {
        bool valid = false;
        bool structuralUsed = false;
        QMatrix4x4 correctionKey;
        QVector<Point3D> reference;   // after voxel/structural downsample AND normals
        SpatialIndex index;
    };
    bool built = false;
    Bounds sourceBounds;
    Bounds targetBounds;
    QVector3D low;
    QVector3D high;
    QVector<Point3D> sourceCrop;
    QVector<Point3D> targetCrop;
    PairInitialData initial;
    QVector<Point3D> movingLevelBase[3];
    // Per-level target artifacts (downsample -> normals -> index). Valid for a
    // subsequent attempt only when its level-entry correction matches the key,
    // which implies an identical overlap box and identical structural decision.
    LevelTargetEntry levelTarget[3];
};

// Restricted ICP configuration of the 2.5D height-map path. It carries the
// height-map candidate as the starting correction and replaces the generic
// correction budget with the height-map budget, so the restricted ICP can only
// refine the candidate and can never leave the strict 10 mm / 1 deg window.
struct RestrictedRegistration {
    bool enabled = false;
    bool restrictedDof = true;
    float maximumTranslationMm = 10.0f;
    float maximumYawDegrees = 1.0f;
    QMatrix4x4 initialCorrection;
    // The 2.5D acceptance uses its own height-map criteria, so the legacy
    // structural 3 mm coverage gate is measured and reported but does not
    // reject the restricted attempt.
    bool bypassLegacyStructuralAcceptance = false;
};

bool buildPlanePrealignment(const PlaneDiagnostic &sourcePlane,
                            const PlaneDiagnostic &targetPlane,
                            const IcpOptions &options,
                            QMatrix4x4 *matrix,
                            IcpDiagnostics *diagnostic) {
    if(!matrix||!diagnostic)return false;
    diagnostic->planePrealignmentAttempted=true;
    if(!sourcePlane.valid||!targetPlane.valid){diagnostic->planePrealignmentReason=QStringLiteral("未找到可匹配的主水平面");return false;}
    const float cosine=qBound(-1.0f,QVector3D::dotProduct(sourcePlane.normal,targetPlane.normal),1.0f);
    const float angle=qRadiansToDegrees(std::acos(cosine));
    diagnostic->planePrealignmentAngleDegrees=angle;
    if(angle>options.maximumPlanePrealignmentAngleDegrees){diagnostic->planePrealignmentReason=QStringLiteral("主平面法向差 %1 deg 超过 %2 deg").arg(angle,0,'g',6).arg(options.maximumPlanePrealignmentAngleDegrees);return false;}
    const QQuaternion rotation=QQuaternion::rotationTo(sourcePlane.normal,targetPlane.normal);
    QMatrix4x4 rotationAboutCentroid;rotationAboutCentroid.setToIdentity();
    rotationAboutCentroid.translate(sourcePlane.centroid);rotationAboutCentroid.rotate(rotation);rotationAboutCentroid.translate(-sourcePlane.centroid);
    const QVector3D rotatedCentroid=rotationAboutCentroid.map(sourcePlane.centroid);
    const float signedDistance=targetPlane.offset-QVector3D::dotProduct(targetPlane.normal,rotatedCentroid);
    diagnostic->planePrealignmentTranslation=std::abs(signedDistance);
    if(std::abs(signedDistance)>options.maximumPlanePrealignmentTranslation){diagnostic->planePrealignmentReason=QStringLiteral("主平面法向平移 %1 mm 超过 %2 mm").arg(std::abs(signedDistance),0,'g',6).arg(options.maximumPlanePrealignmentTranslation);return false;}
    const QVector3D translation=targetPlane.normal*signedDistance;
    QMatrix4x4 translationMatrix;translationMatrix.setToIdentity();translationMatrix.translate(translation);
    *matrix=translationMatrix*rotationAboutCentroid;
    diagnostic->planePrealignmentX=translation.x();diagnostic->planePrealignmentY=translation.y();diagnostic->planePrealignmentZ=translation.z();
    const QVector3D transformedCentroid=matrix->map(sourcePlane.centroid);
    diagnostic->planeResidualAfterPrealignment=targetPlane.offset-QVector3D::dotProduct(targetPlane.normal,transformedCentroid);
    const QVector3D transformedNormal=matrix->mapVector(sourcePlane.normal).normalized();
    diagnostic->planeNormalAngleAfterPrealignment=qRadiansToDegrees(std::acos(qBound(-1.0f,QVector3D::dotProduct(transformedNormal,targetPlane.normal),1.0f)));
    diagnostic->planePrealignmentAccepted=true;
    diagnostic->planePrealignmentReason=QStringLiteral("主平面法向预对齐通过");
    return true;
}

IcpDiagnostics registerPair(const QVector<Point3D> &source,
                            const QVector<Point3D> &target,
                            const IcpOptions &options,
                            int sourceIndex,
                            const QVector3D &stitchAxis,
                            float preferredSourcePlaneHeight,
                            float preferredTargetPlaneHeight,
                            bool planeIdentityTracked,
                            QMatrix4x4 *correction,
                            const ProgressCallback &progress,
                            PairStageTimes *stageTimes = nullptr,
                            PairPreprocessCache *cache = nullptr,
                            const RestrictedRegistration &restricted = {}) {
    IcpDiagnostics diagnostic;
    diagnostic.attempted=true;
    diagnostic.targetCloudIndex=sourceIndex-1;
    diagnostic.sourceCloudIndex=sourceIndex;
    diagnostic.filePath=QStringLiteral("scan %1").arg(sourceIndex+1);
    // Hot-path cancellation: prefer the stable token pointer so the per-iteration
    // ICP loop never touches a std::function when a token is provided. The token
    // object is owned by options.cancellation and outlives this whole call.
    const std::atomic_bool *cancelFlag = options.cancellation.get();
    const auto cancelled = [&]() {
        ++diagnostic.cancellationCheckCount;
        if (cancelFlag) return cancelFlag->load(std::memory_order_relaxed);
        return options.isCancelled && options.isCancelled();
    };
    // The restricted run starts from the height-map candidate instead of the
    // identity, so the accumulated correction keeps the candidate and the trust
    // region below measures the entire strict-budget window.
    if (restricted.enabled) *correction = restricted.initialCorrection;
    else correction->setToIdentity();
    const float budgetTranslation = restricted.enabled
        ? restricted.maximumTranslationMm : options.maximumCorrectionTranslation;
    const float budgetAngleDegrees = restricted.enabled
        ? restricted.maximumYawDegrees : options.maximumCorrectionAngleDegrees;
    const bool restrictedIncrement = restricted.enabled && restricted.restrictedDof;
    // Intra-pair parallelism for the correspondence search. It is independent of
    // the outer frame parallelism: the per-pair loop stays serial and in scan
    // order, only the search inside one iteration is chunked.
    const int correspondenceTasks = options.parallelEnabled
        ? (options.correspondenceParallelTasks > 0
               ? options.correspondenceParallelTasks
               : qMax(1, qMin(4, QThread::idealThreadCount() - 1)))
        : 1;
    diagnostic.icpCorrespondenceParallelTasks = correspondenceTasks;
    RegionTimer stageClock;
    if (stageTimes) stageClock.start(&stageTimes->preprocessMs);
    const QString planeIdentityReason = planeIdentityTracked
        ? QStringLiteral("使用跨帧一致主平面轨迹")
        : QStringLiteral("使用相邻帧局部主平面匹配");
    Bounds sourceBounds, targetBounds;
    QVector3D low, high;
    QVector<Point3D> sourceCrop, targetCrop;
    PlaneDiagnostic sourcePlane, targetPlane;
    if (cache && cache->built && !options.applyWideStructureCandidate) {
        // Relaxed retry: the attempt-invariant preprocessing (bounding boxes,
        // overlap crops, plane fits, initial distance statistics) is reused.
        sourceBounds = cache->sourceBounds;
        targetBounds = cache->targetBounds;
        low = cache->low;
        high = cache->high;
        sourceCrop = cache->sourceCrop;
        targetCrop = cache->targetCrop;
        // The retry shares attempt-invariant preprocessing.  Restore the
        // fitted planes as well; leaving these default-invalid makes the
        // relaxed structural pass report a misleading 0/0 point count.
        sourcePlane = cache->initial.sourcePlane;
        targetPlane = cache->initial.targetPlane;
        stampPairInitialData(&diagnostic, cache->initial, planeIdentityTracked,
            preferredSourcePlaneHeight, preferredTargetPlaneHeight, planeIdentityReason);
    } else {
        sourceBounds=cloudBounds(source);
        targetBounds=cloudBounds(target);
        const float overlapMargin = options.overlapHardConstraint ? 0.0f : options.overlapMargin;
        low=QVector3D(qMax(sourceBounds.minimum.x(),targetBounds.minimum.x())-overlapMargin,
                      qMax(sourceBounds.minimum.y(),targetBounds.minimum.y())-overlapMargin,
                      qMax(sourceBounds.minimum.z(),targetBounds.minimum.z())-overlapMargin);
        high=QVector3D(qMin(sourceBounds.maximum.x(),targetBounds.maximum.x())+overlapMargin,
                       qMin(sourceBounds.maximum.y(),targetBounds.maximum.y())+overlapMargin,
                       qMin(sourceBounds.maximum.z(),targetBounds.maximum.z())+overlapMargin);
        if(high.x()<=low.x()||high.y()<=low.y()||high.z()<=low.z()){
            diagnostic.reason=QStringLiteral("相邻点云包围盒不重叠");return diagnostic;
        }
        sourceCrop=cropCloud(source,low,high);
        targetCrop=cropCloud(target,low,high);
        bool candidateSteepPlane = false;
        PairInitialData initialData=computePairInitialData(sourceCrop,targetCrop,stitchAxis,options,
            preferredSourcePlaneHeight,preferredTargetPlaneHeight,planeIdentityTracked);
        // A steep support plane is not reliably bounded by a global XYZ
        // intersection.  Once a valid tilted plane is visible, rebuild the
        // preparation from complete frames; the common local plane UVN overlap
        // below becomes the single crop/acceptance definition.
        if (initialData.sourcePlane.valid && initialData.targetPlane.valid
            && qMax(initialData.sourcePlane.tiltDegrees, initialData.targetPlane.tiltDegrees) > 36.9f) {
            candidateSteepPlane = true;
            sourceCrop = source;
            targetCrop = target;
            initialData = computePairInitialData(sourceCrop,targetCrop,stitchAxis,options,
                preferredSourcePlaneHeight,preferredTargetPlaneHeight,planeIdentityTracked);
        }
        if (!candidateSteepPlane && (!initialData.sourcePlane.valid || !initialData.targetPlane.valid)
            && (!initialData.sourcePlane.rejectionReason.isEmpty()
                || !initialData.targetPlane.rejectionReason.isEmpty())) {
            // The fixed XYZ crop can contain only a narrow fragment of a
            // steep plane.  Retry fitting on full frames before treating an
            // orientation-independent RANSAC rejection as final.
            const PairInitialData fullData=computePairInitialData(source,target,stitchAxis,options,
                preferredSourcePlaneHeight,preferredTargetPlaneHeight,planeIdentityTracked);
            if (fullData.sourcePlane.valid && fullData.targetPlane.valid
                && qMax(fullData.sourcePlane.tiltDegrees, fullData.targetPlane.tiltDegrees) > 36.9f) {
                candidateSteepPlane = true;
                sourceCrop = source;
                targetCrop = target;
                initialData = fullData;
            }
        }
        sourcePlane=initialData.sourcePlane;
        targetPlane=initialData.targetPlane;
        stampPairInitialData(&diagnostic, initialData, planeIdentityTracked,
            preferredSourcePlaneHeight, preferredTargetPlaneHeight, planeIdentityReason);
        if (cache) {
            cache->built=true;
            cache->sourceBounds=sourceBounds;
            cache->targetBounds=targetBounds;
            cache->low=low;
            cache->high=high;
            cache->sourceCrop=sourceCrop;
            cache->targetCrop=targetCrop;
            cache->initial=initialData;
        }
    }
    if (!sourcePlane.valid || !targetPlane.valid) {
        const QString planeReason = !sourcePlane.rejectionReason.isEmpty()
            ? sourcePlane.rejectionReason : targetPlane.rejectionReason;
        if (planeReason == QStringLiteral("plane_tilt_exceeds_limit")) {
            diagnostic.reason = planeReason;
            diagnostic.failureCode = planeReason;
            diagnostic.steepPlaneRejectionReason = planeReason;
            return diagnostic;
        }
    }
    if(sourceCrop.size()<100||targetCrop.size()<100){diagnostic.reason=QStringLiteral("重叠区抽样点少于 100");return diagnostic;}
    // The restricted run keeps the height-map candidate as its starting value;
    // the plane pre-alignment would replace it.
    if(options.planePrealignmentEnabled && !restricted.enabled){
        QMatrix4x4 prealignment;
        if(sourcePlane.valid&&targetPlane.valid){
            if(!buildPlanePrealignment(sourcePlane,targetPlane,options,&prealignment,&diagnostic)){
                diagnostic.reason=QStringLiteral("主平面预对齐拒绝：%1").arg(diagnostic.planePrealignmentReason);return diagnostic;
            }
            *correction=prealignment;
            QVector<Point3D> prealignedSource=sourceCrop;applyTransform(&prealignedSource,prealignment);
            const DistanceDiagnostic distances=nearestDistanceDiagnostic(prealignedSource,targetCrop);
            diagnostic.nearestDistanceP50AfterPrealignment=distances.p50;diagnostic.nearestDistanceP90AfterPrealignment=distances.p90;diagnostic.nearestDistanceP95AfterPrealignment=distances.p95;diagnostic.coverageWithin3mmAfterPrealignment=distances.within3;diagnostic.coverageWithin5mmAfterPrealignment=distances.within5;diagnostic.coverageWithin10mmAfterPrealignment=distances.within10;
        }else{
            diagnostic.planePrealignmentAttempted=true;
            diagnostic.planePrealignmentReason=QStringLiteral("未找到可匹配的主水平面，保持机器人初值");
        }
    }
    const QMatrix4x4 overlapBaselineCorrection=*correction;
    QVector<Point3D> overlapAlignedSource;
    QVector<Point3D> overlapAlignedFull;
    const Bounds alignedTargetBounds=cloudBounds(target);
    Bounds actualOverlap;
    // Routing decision, taken once per pair: an invalid plane means the legacy
    // robot-XYZ box is used.
    const PlaneDiagnostic overlapPlane=structuralOverlapPlane(targetCrop,sourcePlane,targetPlane,options);
    const bool steepOverlapFrame=overlapPlane.valid;
    const auto rebuildActualOverlap = [&](const QMatrix4x4 &candidateCorrection) {
        overlapAlignedSource = sourceCrop;
        applyTransform(&overlapAlignedSource, candidateCorrection);
        overlapAlignedFull = source;
        applyTransform(&overlapAlignedFull, candidateCorrection);
        if (steepOverlapFrame) {
            const QVector3D origin = overlapPlane.centroid;
            const QVector3D axisU = overlapPlane.axisU;
            const QVector3D axisV = overlapPlane.axisV;
            const QVector3D axisN = overlapPlane.normal;
            const Bounds sourceLocal = projectedBounds(overlapAlignedFull, origin, axisU, axisV, axisN);
            const Bounds targetLocal = projectedBounds(target, origin, axisU, axisV, axisN);
            actualOverlap = intersectBounds(sourceLocal, targetLocal);
            const float overlapMargin = options.overlapHardConstraint ? 0.0f : options.overlapMargin;
            actualOverlap.minimum -= QVector3D(overlapMargin, overlapMargin, overlapMargin);
            actualOverlap.maximum += QVector3D(overlapMargin, overlapMargin, overlapMargin);
            actualOverlap.valid = actualOverlap.maximum.x() > actualOverlap.minimum.x()
                && actualOverlap.maximum.y() > actualOverlap.minimum.y()
                && actualOverlap.maximum.z() > actualOverlap.minimum.z();
            diagnostic.overlapCoordinateSystem = QStringLiteral("plane_uvn");
            diagnostic.overlapLocalMinimum = actualOverlap.minimum;
            diagnostic.overlapLocalMaximum = actualOverlap.maximum;
        } else {
            const Bounds candidateSourceBounds = cloudBounds(overlapAlignedFull);
            actualOverlap.minimum = QVector3D(
                qMax(candidateSourceBounds.minimum.x(), alignedTargetBounds.minimum.x()),
                qMax(candidateSourceBounds.minimum.y(), alignedTargetBounds.minimum.y()),
                qMax(candidateSourceBounds.minimum.z(), alignedTargetBounds.minimum.z()));
            actualOverlap.maximum = QVector3D(
                qMin(candidateSourceBounds.maximum.x(), alignedTargetBounds.maximum.x()),
                qMin(candidateSourceBounds.maximum.y(), alignedTargetBounds.maximum.y()),
                qMin(candidateSourceBounds.maximum.z(), alignedTargetBounds.maximum.z()));
            actualOverlap.valid = actualOverlap.maximum.x() > actualOverlap.minimum.x()
                && actualOverlap.maximum.y() > actualOverlap.minimum.y()
                && actualOverlap.maximum.z() > actualOverlap.minimum.z();
            diagnostic.overlapCoordinateSystem = QStringLiteral("robot_xyz");
        }
        diagnostic.actualOverlapSourceCount = actualOverlap.valid
            ? countInsideBounds(overlapAlignedSource, actualOverlap) : 0;
        diagnostic.actualOverlapTargetCount = actualOverlap.valid
            ? countInsideBounds(targetCrop, actualOverlap) : 0;
        diagnostic.localOverlapSourcePoints = diagnostic.actualOverlapSourceCount;
        diagnostic.localOverlapTargetPoints = diagnostic.actualOverlapTargetCount;
        return actualOverlap.valid
            && diagnostic.actualOverlapSourceCount >= 100
            && diagnostic.actualOverlapTargetCount >= 100;
    };
    if(!rebuildActualOverlap(overlapBaselineCorrection)){
        diagnostic.reason=actualOverlap.valid
            ?QStringLiteral("实际相邻帧重叠区任一侧抽样点少于 100")
            :QStringLiteral("主平面预对齐后相邻帧无实际三维重叠区");
        return diagnostic;
    }
    const auto followCandidateCrop = [&](const QMatrix4x4 &candidateCorrection) {
        if (!actualOverlap.valid) return false;
        QVector<Point3D> aligned = source;
        applyTransform(&aligned, candidateCorrection);
        QVector<Point3D> croppedAligned = cropCloud(aligned, actualOverlap);
        bool invertible = false;
        const QMatrix4x4 inverse = candidateCorrection.inverted(&invertible);
        if (!invertible || croppedAligned.isEmpty()) return false;
        applyTransform(&croppedAligned, inverse);
        QVector<Point3D> candidateTargetCrop = cropCloud(target, actualOverlap);
        if (croppedAligned.size() < 100 || candidateTargetCrop.size() < 100) return false;
        sourceCrop = std::move(croppedAligned);
        targetCrop = std::move(candidateTargetCrop);
        diagnostic.sourceCropCount = sourceCrop.size();
        diagnostic.targetCropCount = targetCrop.size();
        if (cache) {
            cache->movingLevelBase[0].clear();
            cache->movingLevelBase[1].clear();
            cache->movingLevelBase[2].clear();
            for (PairPreprocessCache::LevelTargetEntry &entry : cache->levelTarget)
                entry = PairPreprocessCache::LevelTargetEntry{};
            cache->sourceCrop = sourceCrop;
            cache->targetCrop = targetCrop;
            cache->initial.sourceCropCount = sourceCrop.size();
            cache->initial.targetCropCount = targetCrop.size();
        }
        return true;
    };
    const bool steepPlane = sourcePlane.valid && targetPlane.valid
        && qMax(sourcePlane.tiltDegrees, targetPlane.tiltDegrees) > 36.9f;
    if (steepPlane) {
        QVector<Point3D> alignedSource = overlapAlignedSource;
        PlaneDiagnostic alignedPlane = sourcePlane;
        alignedPlane.normal = correction->mapVector(sourcePlane.normal).normalized();
        alignedPlane.offset = QVector3D::dotProduct(alignedPlane.normal,
                                                     correction->map(sourcePlane.centroid));
        const QVector<Point3D> sourceFeatures = structuralPoints(
            alignedSource, alignedPlane, options.structuralPlaneExclusionMm, actualOverlap);
        const QVector<Point3D> targetFeatures = structuralPoints(
            targetCrop, targetPlane, options.structuralPlaneExclusionMm, actualOverlap);
        diagnostic.steepPlaneFeatureCountSource = voxelDownsample(sourceFeatures, 2.0f).size();
        diagnostic.steepPlaneFeatureCountTarget = voxelDownsample(targetFeatures, 2.0f).size();
        if (diagnostic.steepPlaneFeatureCountSource < options.minimumStructuralPoints
            || diagnostic.steepPlaneFeatureCountTarget < options.minimumStructuralPoints) {
            diagnostic.steepPlaneRejectionReason = QStringLiteral(
                "steep_plane_feature_insufficient: feature points %1/%2 < %3")
                .arg(diagnostic.steepPlaneFeatureCountSource)
                .arg(diagnostic.steepPlaneFeatureCountTarget)
                .arg(options.minimumStructuralPoints);
            diagnostic.reason = diagnostic.steepPlaneRejectionReason;
            return diagnostic;
        }
    }
    bool runTangentCoarse = options.tangentCoarseAlignmentEnabled;
    if (runTangentCoarse || (options.tangentCoarseAlignmentAuto && sourcePlane.valid && targetPlane.valid)) {
        if (!runTangentCoarse) {
            // Balanced mode trigger: the robot initial value (after plane
            // pre-alignment) still misses the accepted overlap/coverage
            // conditions of this pair.
            const bool coverageKnown = diagnostic.coverageWithin3mmAfterPrealignment > 0.0f
                || diagnostic.nearestDistanceP90AfterPrealignment > 0.0f;
            if (coverageKnown
                && (diagnostic.coverageWithin3mmAfterPrealignment + 1.0e-4f
                        < options.minimumDegenerateStructuralCoverage3mm
                    || diagnostic.nearestDistanceP90AfterPrealignment
                        > options.maximumCorrectionTranslation)) {
                runTangentCoarse = true;
            }
        }
    }
    if (stageTimes) { stageClock.stop(); stageClock.start(&stageTimes->coarseSearchMs); }
    if(runTangentCoarse&&sourcePlane.valid&&targetPlane.valid){
        PlaneDiagnostic alignedSourcePlane=sourcePlane;
        alignedSourcePlane.normal=(*correction).mapVector(sourcePlane.normal).normalized();
        alignedSourcePlane.offset=QVector3D::dotProduct(alignedSourcePlane.normal,(*correction).map(sourcePlane.centroid));
        QVector<Point3D> sourceStructural=structuralPoints(overlapAlignedSource,alignedSourcePlane,options.structuralPlaneExclusionMm,actualOverlap);
        const QVector<Point3D> targetStructural=structuralPoints(targetCrop,targetPlane,options.structuralPlaneExclusionMm,actualOverlap);
        QMatrix4x4 coarseRotation;
        const QVector3D coarse=tangentCoarseTranslation(sourceStructural,targetStructural,targetPlane,options,&diagnostic,&coarseRotation);
        if(diagnostic.tangentCoarseAlignmentApplied){
            QMatrix4x4 coarseMatrix;coarseMatrix.setToIdentity();coarseMatrix.translate(coarse);
            const QMatrix4x4 coarseIncrement=coarseRotation*coarseMatrix;
            const QMatrix4x4 correctionBeforeCoarse=*correction;
            bool coarseAccepted=false;
            float acceptedFactor=0.0f;
            const float factors[4]{1.0f,0.5f,0.25f,0.125f};
            for(float factor:factors){
                const QMatrix4x4 candidateCorrection=
                    scaledRigidIncrement(coarseIncrement,factor)*correctionBeforeCoarse;
                if(translationLength(candidateCorrection)>budgetTranslation
                    ||rotationAngle(candidateCorrection)>budgetAngleDegrees)
                    continue;
                if(!rebuildActualOverlap(candidateCorrection))continue;
                *correction=candidateCorrection;
                coarseAccepted=true;
                acceptedFactor=factor;
                if (!followCandidateCrop(*correction)) {
                    coarseAccepted=false;
                    *correction=correctionBeforeCoarse;
                    rebuildActualOverlap(*correction);
                }
                break;
            }
            if(!coarseAccepted){
                diagnostic.tangentCoarseAlignmentApplied=false;
                diagnostic.tangentCoarseAlignmentReason=QStringLiteral(
                    "切向粗对齐候选均超出 %1 mm / %2 deg 安全范围或破坏实际重叠，保持主平面预对齐")
                    .arg(options.maximumCorrectionTranslation).arg(options.maximumCorrectionAngleDegrees);
                *correction=correctionBeforeCoarse;
                rebuildActualOverlap(*correction);
            }else if(acceptedFactor<0.999f){
                diagnostic.tangentCoarseAlignmentReason+=QStringLiteral("；按安全预算缩放为 %1")
                    .arg(acceptedFactor,0,'g',3);
            }
        }
    }else diagnostic.tangentCoarseAlignmentReason=options.tangentCoarseAlignmentEnabled
        ?QStringLiteral("未检测到可用主平面，跳过切向粗对齐")
        :(options.tangentCoarseAlignmentAuto
            ?QStringLiteral("机器人初值已满足重叠/覆盖率条件，切向粗对齐保持关闭")
            :QStringLiteral("切向粗对齐已禁用"));
    if(options.wideStructureDiagnosticEnabled&&sourcePlane.valid&&targetPlane.valid){
        PlaneDiagnostic alignedSourcePlane=sourcePlane;
        alignedSourcePlane.normal=(*correction).mapVector(sourcePlane.normal).normalized();
        alignedSourcePlane.offset=QVector3D::dotProduct(alignedSourcePlane.normal,(*correction).map(sourcePlane.centroid));
        QVector<Point3D> wideSource=source;applyTransform(&wideSource,*correction);
        QMatrix4x4 wideCandidate;
        runWideStructureDiagnostic(pureStructuralPoints(wideSource,alignedSourcePlane,options.structuralPlaneExclusionMm),
            pureStructuralPoints(target,targetPlane,options.structuralPlaneExclusionMm),targetPlane,options,&diagnostic,&wideCandidate);
        if(options.applyWideStructureCandidate&&diagnostic.wideStructureDiagnosticValid
            &&diagnostic.wideStructureCoverageBest>diagnostic.wideStructureCoverageBefore+options.tangentCoarseMinimumCoverageGain){
            const QMatrix4x4 correctionBeforeWide=*correction;
            const QMatrix4x4 candidateCorrection=wideCandidate*(*correction);
            if(translationLength(candidateCorrection)<=budgetTranslation
                &&rotationAngle(candidateCorrection)<=budgetAngleDegrees
                &&rebuildActualOverlap(candidateCorrection)){
                if (!followCandidateCrop(candidateCorrection)) {
                    diagnostic.wideStructureDiagnosticReason = QStringLiteral(
                        "大范围结构候选重裁剪后点数不足，未应用");
                    rebuildActualOverlap(correctionBeforeWide);
                } else {
                    *correction=candidateCorrection;
                }
            }else{
                diagnostic.wideStructureDiagnosticReason=QStringLiteral("大范围结构候选超出修正预算或实际重叠点不足，未应用");
                *correction=correctionBeforeWide;
                rebuildActualOverlap(*correction);
            }
        }
    }else diagnostic.wideStructureDiagnosticReason=options.wideStructureDiagnosticEnabled
        ?QStringLiteral("未检测到可用主平面，跳过大范围结构诊断"):QStringLiteral("大范围结构诊断已禁用");
    if (stageTimes) stageClock.stop();
    const int maximumIterations[3]{60,45,35};
    int levelAcceptedIterations[3]{0,0,0};
    for(int level=0;level<3;++level){
        if (stageTimes) stageClock.start(&stageTimes->levelMs[level]);
        float previousFitness=-1.0f;
        float previousRmse=std::numeric_limits<float>::max();
        const float voxel=options.voxelLevels[level];
        float distance=options.correspondenceDistances[level];
        // The pre-alignment residual is the only pre-ICP visibility signal the
        // pair has: when it is below the acceptance gate the pair is in a hard
        // regime, and two adaptations below become active (wider first-level
        // basin, trimmed correspondence set). Well behaved pairs keep the legacy
        // numbers, so their corrections stay untouched.
        const float prealignmentCoverage = diagnostic.coverageWithin3mmAfterPrealignment>0.0f
            ? diagnostic.coverageWithin3mmAfterPrealignment : diagnostic.coverageWithin3mm;
        const bool hardRegime = prealignmentCoverage>0.0f
            && prealignmentCoverage<options.minimumDegenerateStructuralCoverage3mm;
        // A pre-alignment p90 beyond the first-level correspondence radius means
        // part of the surface starts outside the ICP basin; only then is the
        // first level widened, so a pair that is already inside the basin keeps
        // the historical numbers.
        if(level==0 && options.adaptiveCorrespondenceRadius
            && options.adaptiveCorrespondenceRadiusMm>distance
            && diagnostic.nearestDistanceP90AfterPrealignment>2.0f){
            distance=options.adaptiveCorrespondenceRadiusMm;
            diagnostic.adaptiveCorrespondenceRadiusUsed=true;
        }
        float trimFraction=1.0f;
        if(options.correspondenceTrimFraction>0.0f){
            trimFraction=qBound(0.05f,options.correspondenceTrimFraction,1.0f);
        }else if(options.autoTrimCorrespondences&&hardRegime){
            trimFraction=float(qBound(0.25,2.0*double(prealignmentCoverage),0.9));
        }
        if(trimFraction<0.999f)diagnostic.correspondenceTrimFractionUsed=trimFraction;
        QVector<Point3D> moving;
        if (cache && cache->built && !cache->movingLevelBase[level].isEmpty()) {
            moving = cache->movingLevelBase[level];
        } else {
            moving = voxelDownsample(sourceCrop, voxel);
            // The normal-compatibility gate needs a source normal as well; the
            // estimate is cached with the level base so a relaxed retry reuses
            // it instead of recomputing it.
            if(options.normalCompatibilityDegrees>0.0f)
                estimateNormals(&moving,qMax(voxel*4.0f,distance*1.5f));
            if (cache && cache->built && !moving.isEmpty())
                cache->movingLevelBase[level] = moving;
        }
        PairPreprocessCache::LevelTargetEntry *levelCache =
            (cache && cache->built) ? &cache->levelTarget[level] : nullptr;
        const bool referenceCached = levelCache && levelCache->valid
            && levelCache->correctionKey == *correction;
        QVector<Point3D> reference;
        SpatialIndex index;
        bool structuralUsed = false;
        int baselineInside = 0;
        if (!referenceCached) {
            RegionTimer targetPrepClock;
            if (stageTimes) targetPrepClock.start(&stageTimes->targetLevelCacheMs);
            reference=voxelDownsample(cropCloud(targetCrop,actualOverlap),voxel);
            if(moving.size()<100||reference.size()<100){diagnostic.reason=QStringLiteral("%1 mm 层点数少于 100").arg(voxel);return diagnostic;}
            applyTransform(&moving,*correction);
            if(options.structuralIcpEnabled&&sourcePlane.valid&&targetPlane.valid){
                PlaneDiagnostic alignedSourcePlane=sourcePlane;
                alignedSourcePlane.normal=(*correction).mapVector(sourcePlane.normal).normalized();
                alignedSourcePlane.offset=QVector3D::dotProduct(alignedSourcePlane.normal,(*correction).map(sourcePlane.centroid));
                if (steepPlane) {
                    alignedSourcePlane.centroid=targetPlane.centroid;
                    alignedSourcePlane.axisU=targetPlane.axisU;
                    alignedSourcePlane.axisV=targetPlane.axisV;
                }
                const QVector<Point3D> structuralMoving=structureWeightedPoints(moving,alignedSourcePlane,options.structuralPlaneExclusionMm,options.supportPlaneSampleStride,steepPlane);
                const QVector<Point3D> structuralReference=structureWeightedPoints(reference,targetPlane,options.structuralPlaneExclusionMm,options.supportPlaneSampleStride,steepPlane);
                if(structuralMoving.size()>=options.minimumStructuralPoints&&structuralReference.size()>=options.minimumStructuralPoints){
                    moving=voxelDownsample(structuralMoving,voxel);reference=voxelDownsample(structuralReference,voxel);
                    structuralUsed=true;
                    diagnostic.structuralIcpUsed=true;
                    diagnostic.structuralIcpReason=QStringLiteral("ICP 保留全部结构点，主支撑平面每 %1 点保留 1 点").arg(options.supportPlaneSampleStride);
                }else diagnostic.structuralIcpReason=QStringLiteral("结构点少于 %1，ICP 保留重叠点集").arg(options.minimumStructuralPoints);
            }else diagnostic.structuralIcpReason=options.structuralIcpEnabled?QStringLiteral("未检测到可用主平面，ICP 保留重叠点集"):QStringLiteral("结构点 ICP 已禁用");
            baselineInside=countInsideBounds(moving,actualOverlap);
            if(baselineInside<100){diagnostic.reason=QStringLiteral("%1 mm 层实际重叠区源点少于 100").arg(voxel);return diagnostic;}
            estimateNormals(&reference,qMax(voxel*4.0f,distance*1.5f));
            index=buildIndex(reference,distance);
            if (levelCache) {
                levelCache->valid=true;
                levelCache->structuralUsed=structuralUsed;
                levelCache->correctionKey=*correction;
                levelCache->reference=reference;
                levelCache->index=index;
            }
            if (stageTimes) targetPrepClock.stop();
        } else {
            // Shared strict/retry target cache: downsample, normals and index
            // of this level are reused; the source side is re-derived because
            // moving is transformed per attempt.
            reference = levelCache->reference;
            index = levelCache->index;
            structuralUsed = levelCache->structuralUsed;
            diagnostic.targetLevelCacheHit = true;
            applyTransform(&moving,*correction);
            if (structuralUsed) {
                PlaneDiagnostic alignedSourcePlane=sourcePlane;
                alignedSourcePlane.normal=(*correction).mapVector(sourcePlane.normal).normalized();
                alignedSourcePlane.offset=QVector3D::dotProduct(alignedSourcePlane.normal,(*correction).map(sourcePlane.centroid));
                if (steepPlane) {
                    alignedSourcePlane.centroid=targetPlane.centroid;
                    alignedSourcePlane.axisU=targetPlane.axisU;
                    alignedSourcePlane.axisV=targetPlane.axisV;
                }
                const QVector<Point3D> structuralMoving=structureWeightedPoints(moving,alignedSourcePlane,options.structuralPlaneExclusionMm,options.supportPlaneSampleStride,steepPlane);
                moving=voxelDownsample(structuralMoving,voxel);
                diagnostic.structuralIcpUsed=true;
                diagnostic.structuralIcpReason=QStringLiteral("ICP 保留全部结构点，主支撑平面每 %1 点保留 1 点").arg(options.supportPlaneSampleStride);
            }else diagnostic.structuralIcpReason=options.structuralIcpEnabled?QStringLiteral("未检测到可用主平面，ICP 保留重叠点集"):QStringLiteral("结构点 ICP 已禁用");
            baselineInside=countInsideBounds(moving,actualOverlap);
            if(baselineInside<100){diagnostic.reason=QStringLiteral("%1 mm 层实际重叠区源点少于 100").arg(voxel);return diagnostic;}
        }
        FeatureResidualOptions featureResiduals;
        if (!restrictedIncrement && steepPlane && structuralUsed && targetPlane.valid) {
            featureResiduals.enabled=true;
            featureResiduals.normal=targetPlane.normal;
            featureResiduals.origin=targetPlane.centroid;
            featureResiduals.axisU=targetPlane.axisU;
            featureResiduals.axisV=targetPlane.axisV;
            featureResiduals.offset=targetPlane.offset;
            featureResiduals.planeExclusion=options.structuralPlaneExclusionMm;
            featureResiduals.boundaryBounds=projectedBounds(reference,targetPlane.centroid,
                targetPlane.axisU,targetPlane.axisV,targetPlane.normal);
        }
        const int minimumInside=qMax(100,int(std::ceil(double(baselineInside)*diagnostic.minimumOverlapRetention)));
        Increment last;
        std::array<std::array<double,6>,6> lockedEigenvectors{};
        std::array<double,6> lockedEigenvalues{};
        bool degeneracyBasisLocked=false;
        QVector<CorrespondencePair> correspondenceScratch;
        std::unordered_set<int> uniqueReferenceScratch;
        QVector<float> residualScratch;
        int stallIterations=0;
        RegionTimer correspondenceClock;
        RegionTimer boundaryClock;
        for(int iteration=0;iteration<maximumIterations[level];++iteration){
            if(cancelled()){diagnostic.reason=QStringLiteral("已取消");return diagnostic;}
            if (stageTimes) correspondenceClock.start(&stageTimes->correspondenceMs);
            last=estimateIncrement(moving,reference,index,distance,actualOverlap,
                &correspondenceScratch,&uniqueReferenceScratch,&featureResiduals,
                options.normalCompatibilityDegrees,trimFraction,&residualScratch,
                &diagnostic.normalRejectedCorrespondences,correspondenceTasks,restrictedIncrement);
            if (stageTimes) correspondenceClock.stop();
            if(!last.ok){diagnostic.reason=QStringLiteral("%1 mm 层有效对应少于 100").arg(voxel);return diagnostic;}
            // Finite-value gate: a non-finite increment never reaches the trust
            // region, the overlap trial or the output.
            if(!std::isfinite(last.rmse)||!std::isfinite(last.xyRmse)
                ||!std::isfinite(last.zRmse)||!std::isfinite(last.conditionRatio)){
                diagnostic.reason=QStringLiteral("ICP 增量包含非有限值");return diagnostic;
            }
            for(int element=0;element<16;++element){
                if(!std::isfinite(last.matrix.constData()[element])){
                    diagnostic.reason=QStringLiteral("ICP 增量矩阵包含非有限值");return diagnostic;
                }
            }
            if(restrictedIncrement){
                // The restricted solver already returns an increment inside its
                // own four-dimensional subspace, so the six-dimensional locked
                // basis projection is skipped rather than re-applied.
            }else if(!degeneracyBasisLocked){
                lockedEigenvectors=last.eigenvectors;lockedEigenvalues=last.eigenvalues;
                degeneracyBasisLocked=true;
            }else{
                double projected[6]{};
                for(int direction=0;direction<6;++direction){
                    if(lockedEigenvalues[size_t(direction)]<1.0e-3)continue;
                    double coefficient=0.0;for(int row=0;row<6;++row)coefficient+=lockedEigenvectors[size_t(row)][size_t(direction)]*last.normalizedUpdate[size_t(row)];
                    for(int row=0;row<6;++row)projected[row]+=lockedEigenvectors[size_t(row)][size_t(direction)]*coefficient;
                }
                const QVector3D projectedRotation(float(projected[0]/last.rotationScale),float(projected[1]/last.rotationScale),float(projected[2]/last.rotationScale));
                const float projectedAngle=projectedRotation.length();
                QMatrix4x4 constrained;constrained.setToIdentity();
                constrained.translate(last.rotationCenter);
                if(projectedAngle>1.0e-12f)constrained.rotate(QQuaternion::fromAxisAndAngle(projectedRotation/projectedAngle,qRadiansToDegrees(projectedAngle)));
                constrained.translate(-last.rotationCenter);
                constrained(0,3)+=float(projected[3]);constrained(1,3)+=float(projected[4]);constrained(2,3)+=float(projected[5]);
                last.matrix=constrained;
            }
            diagnostic.observableDof=last.observableDof;
            diagnostic.conditionRatio=last.conditionRatio;
            diagnostic.degenerate=diagnostic.degenerate||last.degenerate;
            if(last.degenerate)++diagnostic.projectedIterations;
            QMatrix4x4 acceptedIncrement;
            bool overlapStepAccepted=false;
            bool overlapCandidateAvailable=false;
            const float stepFactors[4]{1.0f,0.5f,0.25f,0.125f};
            if (stageTimes) boundaryClock.start(&stageTimes->boundaryCheckMs);
            for(float factor:stepFactors){
                const QMatrix4x4 candidate=scaledRigidIncrement(last.matrix,factor);
                if(countInsideBoundsTransformed(moving,candidate,actualOverlap,minimumInside)>=minimumInside){
                    overlapCandidateAvailable=true;
                    const QMatrix4x4 candidateCorrection=candidate*(*correction);
                    if(translationLength(candidateCorrection)>budgetTranslation
                        ||rotationAngle(candidateCorrection)>budgetAngleDegrees){
                        ++diagnostic.safetyConstrainedStepReductions;continue;
                    }
                    acceptedIncrement=candidate;overlapStepAccepted=true;
                    if(factor<0.999f)++diagnostic.overlapConstrainedStepReductions;
                    break;
                }
            }
            if (stageTimes) boundaryClock.stop();
            if(!overlapStepAccepted){
                if(overlapCandidateAvailable){diagnostic.stoppedAtSafetyBoundary=true;break;}
                diagnostic.reason=QStringLiteral("ICP 增量会使源点滑出实际相邻帧重叠区");return diagnostic;
            }
            last.matrix=acceptedIncrement;
            applyTransform(&moving,acceptedIncrement);
            *correction=acceptedIncrement*(*correction);
            ++diagnostic.iterations;
            ++levelAcceptedIterations[level];
            const float translation=translationLength(*correction);
            const float angle=rotationAngle(*correction);
            if(translation>budgetTranslation||angle>budgetAngleDegrees){diagnostic.reason=QStringLiteral("内部错误：ICP 信赖域越过安全范围");return diagnostic;}
            const float fitness=float(last.correspondences)/qMax(1,moving.size());
            const float fitnessChange=previousFitness<0?std::numeric_limits<float>::max():std::abs(fitness-previousFitness)/qMax(1.0e-12f,std::abs(previousFitness));
            const float rmseChange=std::isfinite(previousRmse)?std::abs(last.rmse-previousRmse)/qMax(1.0e-12f,std::abs(previousRmse)):std::numeric_limits<float>::max();
            previousFitness=fitness;previousRmse=last.rmse;
            if(fitnessChange<=1.0e-7f&&rmseChange<=1.0e-7f){diagnostic.converged=true;break;}
            // No-improvement termination: a level that only produces
            // sub-micron accepted increments is effectively converged. The
            // final safety and structural acceptance still gate the result.
            if(translationLength(acceptedIncrement)<=0.005f&&rotationAngle(acceptedIncrement)<=0.0005f){
                if(++stallIterations>=8){diagnostic.converged=true;break;}
            }else stallIterations=0;
        }
        diagnostic.completedLevels=level+1;
        diagnostic.movingSampleCount=moving.size();
        diagnostic.correspondences=last.correspondences;
        diagnostic.fitness=float(last.correspondences)/qMax(1,moving.size());
        diagnostic.rmse=last.rmse;diagnostic.xyRmse=last.xyRmse;diagnostic.zRmse=last.zRmse;
        diagnostic.uniqueReferenceRatio=float(last.uniqueReferences)/qMax(1,last.correspondences);
        diagnostic.duplicateCorrespondenceRatio=1.0f-diagnostic.uniqueReferenceRatio;
        diagnostic.levelFitness.push_back(diagnostic.fitness);
        diagnostic.levelRmse.push_back(last.rmse);
        diagnostic.levelCorrespondences.push_back(last.correspondences);
        diagnostic.levelIterations.push_back(levelAcceptedIterations[level]);
        if(progress)progress(float(level+1)/3.0f,QStringLiteral("scan %1：%2 mm Point-to-Plane ICP").arg(sourceIndex+1).arg(voxel));
        if (stageTimes) stageClock.stop();
    }
    diagnostic.converged=true;
    diagnostic.correctionTranslation=translationLength(*correction);
    diagnostic.correctionAngleDegrees=rotationAngle(*correction);
    diagnostic.correctionX=(*correction)(0,3);diagnostic.correctionY=(*correction)(1,3);diagnostic.correctionZ=(*correction)(2,3);
    diagnostic.overlapRatio=diagnostic.fitness;
    if (stageTimes) stageClock.start(&stageTimes->structuralValidationMs);
    if(options.structuralValidationEnabled){
        const auto validateStructural = [&](float exclusion,
                                             QVector<Point3D> *sourceOutput,
                                             QVector<Point3D> *targetOutput) {
            const bool steepPlane = sourcePlane.valid && targetPlane.valid
                && qMax(sourcePlane.tiltDegrees, targetPlane.tiltDegrees) > 36.9f;
            if (steepPlane) {
                QVector<Point3D> alignedSource = sourceCrop;
                applyTransform(&alignedSource, *correction);
                PlaneDiagnostic alignedPlane = sourcePlane;
                alignedPlane.normal = correction->mapVector(sourcePlane.normal).normalized();
                alignedPlane.offset = QVector3D::dotProduct(alignedPlane.normal,
                                                             correction->map(sourcePlane.centroid));
                *sourceOutput = structuralPoints(alignedSource, alignedPlane, exclusion, actualOverlap);
            } else {
                *sourceOutput=structuralPoints(sourceCrop,sourcePlane,exclusion,
                    Bounds{sourceBounds.minimum,sourceBounds.maximum,true});
                applyTransform(sourceOutput,*correction);
            }
            *sourceOutput=cropCloud(*sourceOutput,actualOverlap);
            *targetOutput=structuralPoints(targetCrop,targetPlane,exclusion,actualOverlap);
        };
        QVector<Point3D> sourceStructural,targetStructural;
        validateStructural(options.structuralPlaneExclusionMm,&sourceStructural,&targetStructural);
        DistanceDiagnostic sourceToTarget=nearestDistanceDiagnostic(sourceStructural,targetStructural);
        DistanceDiagnostic targetToSource=nearestDistanceDiagnostic(targetStructural,sourceStructural);
        // Evidence floor: an absolute count plus a share of the cropped
        // structural points, so a pair is never judged on a handful of hits.
        const auto evidenceFloor = [&](const DistanceDiagnostic &distances) {
            return qMax(options.minimumStructuralPoints,
                        int(std::ceil(double(options.minimumStructuralMatchedRatio)
                                      * double(qMax(1,distances.total)))));
        };
        // Central stamping of every structural metric: the legacy total-point
        // fields stay byte-for-byte what the hand-written report and the
        // regression baseline expect, and the matched-point view is added next
        // to them. Both the strict pass and the 2 mm relaxed pass reuse this.
        const auto stampStructuralDistances = [&](const DistanceDiagnostic &s2t,
                                                  const DistanceDiagnostic &t2s) {
            diagnostic.structuralSourcePoints=s2t.total;
            diagnostic.structuralTargetPoints=t2s.total;
            diagnostic.structuralSourceMatchedPoints=s2t.matched;
            diagnostic.structuralTargetMatchedPoints=t2s.matched;
            diagnostic.structuralSourceToTargetP50=s2t.p50;
            diagnostic.structuralSourceToTargetP90=s2t.p90;
            diagnostic.structuralSourceToTargetP95=s2t.p95;
            diagnostic.structuralTargetToSourceP50=t2s.p50;
            diagnostic.structuralTargetToSourceP90=t2s.p90;
            diagnostic.structuralTargetToSourceP95=t2s.p95;
            diagnostic.structuralSourceToTargetCoverage3mm=s2t.within3;
            diagnostic.structuralSourceToTargetCoverage5mm=s2t.within5;
            diagnostic.structuralSourceToTargetCoverage10mm=s2t.within10;
            diagnostic.structuralTargetToSourceCoverage3mm=t2s.within3;
            diagnostic.structuralTargetToSourceCoverage5mm=t2s.within5;
            diagnostic.structuralTargetToSourceCoverage10mm=t2s.within10;
            diagnostic.structuralBidirectionalCoverage3mm=qMin(s2t.within3,t2s.within3);
            diagnostic.structuralBidirectionalCoverage5mm=qMin(s2t.within5,t2s.within5);
            diagnostic.structuralSourceToTargetCoverage3mmOfMatched=s2t.within3OfMatched();
            diagnostic.structuralSourceToTargetCoverage5mmOfMatched=s2t.within5OfMatched();
            diagnostic.structuralTargetToSourceCoverage3mmOfMatched=t2s.within3OfMatched();
            diagnostic.structuralTargetToSourceCoverage5mmOfMatched=t2s.within5OfMatched();
            diagnostic.structuralBidirectionalCoverage3mmOfMatched=
                qMin(s2t.within3OfMatched(),t2s.within3OfMatched());
            diagnostic.structuralBidirectionalCoverage5mmOfMatched=
                qMin(s2t.within5OfMatched(),t2s.within5OfMatched());
            diagnostic.structuralSourceMatchRatio=s2t.matchedRatio();
            diagnostic.structuralTargetMatchRatio=t2s.matchedRatio();
            diagnostic.structuralBidirectionalMatchRatio=qMin(s2t.matchedRatio(),t2s.matchedRatio());
            diagnostic.structuralSourceMatchedEvidenceFloor=evidenceFloor(s2t);
            diagnostic.structuralTargetMatchedEvidenceFloor=evidenceFloor(t2s);
        };
        // Default rule: evidence floor on the matched counts, then accuracy
        // among the matched points. The opt-in legacy rule keeps the historical
        // total-point counts and total-point ratios unchanged.
        const auto evaluateAcceptance = [&](const DistanceDiagnostic &s2t,
                                            const DistanceDiagnostic &t2s,
                                            bool relaxedBranch,
                                            bool *enoughStructure,
                                            bool *enoughCoverage) {
            *enoughStructure=false;*enoughCoverage=false;
            const bool countsOk = options.structuralCoverageOfMatched
                ? (s2t.matched>=evidenceFloor(s2t) && t2s.matched>=evidenceFloor(t2s))
                : (s2t.total>=options.minimumStructuralPoints
                   && t2s.total>=options.minimumStructuralPoints);
            if(!countsOk)return;
            *enoughStructure=true;
            const int minimumStructure = options.structuralCoverageOfMatched
                ? qMin(s2t.matched,t2s.matched) : qMin(s2t.total,t2s.total);
            const float coverage3 = options.structuralCoverageOfMatched
                ? diagnostic.structuralBidirectionalCoverage3mmOfMatched
                : diagnostic.structuralBidirectionalCoverage3mm;
            const float coverage5 = options.structuralCoverageOfMatched
                ? diagnostic.structuralBidirectionalCoverage5mmOfMatched
                : diagnostic.structuralBidirectionalCoverage5mm;
            const bool lowCountThresholds = relaxedBranch
                ? !diagnostic.steepPlaneDetected
                : (options.relaxedStructuralValidation && minimumStructure<500
                   && !diagnostic.steepPlaneDetected);
            if(lowCountThresholds){
                *enoughCoverage=coverage3>=options.minimumLowCountStructuralCoverage3mm
                    &&coverage5>=options.minimumLowCountStructuralCoverage5mm;
            }else{
                *enoughCoverage=coverage3>=options.minimumDegenerateStructuralCoverage3mm;
            }
        };
        stampStructuralDistances(sourceToTarget,targetToSource);
        diagnostic.steepPlaneDetected = sourcePlane.valid && targetPlane.valid
            && qMax(sourcePlane.tiltDegrees, targetPlane.tiltDegrees) > 36.9f;
        diagnostic.steepPlaneFeatureCountSource = sourceToTarget.total;
        diagnostic.steepPlaneFeatureCountTarget = targetToSource.total;
        diagnostic.steepPlaneObservableDof = diagnostic.observableDof;
        if (diagnostic.steepPlaneDetected
            && (diagnostic.observableDof < options.steepPlaneMinimumObservableDof
                || sourceToTarget.total < options.minimumStructuralPoints
                || targetToSource.total < options.minimumStructuralPoints)) {
            diagnostic.steepPlaneRejectionReason = diagnostic.observableDof < options.steepPlaneMinimumObservableDof
                ? QStringLiteral("steep_plane_feature_insufficient: observableDof=%1 < %2")
                    .arg(diagnostic.observableDof).arg(options.steepPlaneMinimumObservableDof)
                : QStringLiteral("steep_plane_feature_insufficient: feature points %1/%2 < %3")
                    .arg(sourceToTarget.total).arg(targetToSource.total).arg(options.minimumStructuralPoints);
            diagnostic.structuralValidationReason = diagnostic.steepPlaneRejectionReason;
            diagnostic.reason = diagnostic.steepPlaneRejectionReason;
            diagnostic.failureCode = QStringLiteral("steep_plane_feature_insufficient");
            return diagnostic;
        }
        bool enoughStructure=false,enoughCoverage=false;
        evaluateAcceptance(sourceToTarget,targetToSource,false,&enoughStructure,&enoughCoverage);
        if(!enoughStructure && options.relaxedStructuralValidation){
            // The relaxed pass may recover points hidden by the strict +/-3 mm plane band.
            validateStructural(2.0f,&sourceStructural,&targetStructural);
            sourceToTarget=nearestDistanceDiagnostic(sourceStructural,targetStructural);
            targetToSource=nearestDistanceDiagnostic(targetStructural,sourceStructural);
            stampStructuralDistances(sourceToTarget,targetToSource);
            evaluateAcceptance(sourceToTarget,targetToSource,true,&enoughStructure,&enoughCoverage);
        }
        diagnostic.structuralCoverageMode = options.structuralCoverageOfMatched
            ? QStringLiteral("matched_ratio") : QStringLiteral("legacy_total");
        diagnostic.acceptanceRule = options.structuralCoverageOfMatched
            ? QStringLiteral("matched>=max(%1, %2 pct of cropped structural) && min(cov3_of_matched)>=%3")
                .arg(options.minimumStructuralPoints)
                .arg(QString::number(double(options.minimumStructuralMatchedRatio)*100.0,'g',4))
                .arg(QString::number(double(options.minimumDegenerateStructuralCoverage3mm),'g',4))
            : QStringLiteral("total>=%1 && min(cov3_of_total)>=%2")
                .arg(options.minimumStructuralPoints)
                .arg(QString::number(double(options.minimumDegenerateStructuralCoverage3mm),'g',4));
        // Visibility is reported, never used to reject: a low matched ratio
        // means the pair is verified on a small subset, which the operator must
        // see in the report.
        diagnostic.visibilityWarning =
            diagnostic.structuralBidirectionalMatchRatio < options.visibilityWarningRatio;
        if(!enoughStructure||!enoughCoverage){
            if(!enoughStructure){
                diagnostic.failureCode=QStringLiteral("structural_points_insufficient");
                diagnostic.structuralValidationReason=options.structuralCoverageOfMatched
                    ? QStringLiteral("结构点命中不足（structural_points_insufficient）：命中 %1/%2 与 %3/%4，证据下限 %5/%6")
                        .arg(sourceToTarget.matched).arg(sourceToTarget.total)
                        .arg(targetToSource.matched).arg(targetToSource.total)
                        .arg(diagnostic.structuralSourceMatchedEvidenceFloor)
                        .arg(diagnostic.structuralTargetMatchedEvidenceFloor)
                    : QStringLiteral("重叠区结构点不足：%1/%2，最低各 %3")
                        .arg(sourceToTarget.total).arg(targetToSource.total).arg(options.minimumStructuralPoints);
            }else{
                diagnostic.failureCode=QStringLiteral("structural_coverage_insufficient");
                diagnostic.structuralValidationReason=options.structuralCoverageOfMatched
                    ? QStringLiteral("结构点覆盖率不足（structural_coverage_insufficient）：命中点内双向 3 mm=%1，5 mm=%2")
                        .arg(diagnostic.structuralBidirectionalCoverage3mmOfMatched,0,'f',4)
                        .arg(diagnostic.structuralBidirectionalCoverage5mmOfMatched,0,'f',4)
                    : QStringLiteral("结构点覆盖率不足：双向 3 mm=%1，5 mm=%2")
                        .arg(diagnostic.structuralBidirectionalCoverage3mm,0,'f',4)
                        .arg(diagnostic.structuralBidirectionalCoverage5mm,0,'f',4);
            }
            diagnostic.reason=diagnostic.structuralValidationReason;
            // The 2.5D acceptance has its own height-map criteria: the legacy
            // structural 3 mm coverage gate is measured, stamped and reported,
            // but it does not reject the restricted attempt. The height-map
            // acceptance decides after the restricted ICP returns.
            if(!restricted.bypassLegacyStructuralAcceptance)return diagnostic;
            diagnostic.structuralValidationPassed=false;
        } else {
            diagnostic.structuralValidationPassed=true;
            diagnostic.structuralValidationReason=options.structuralCoverageOfMatched
                ? QStringLiteral("已通过结构点双向覆盖验收（命中点内 3 mm=%1，命中率=%2）")
                    .arg(diagnostic.structuralBidirectionalCoverage3mmOfMatched,0,'f',4)
                    .arg(diagnostic.structuralBidirectionalMatchRatio,0,'f',4)
                : QStringLiteral("已通过结构点双向覆盖验收");
        }
    }else{
        diagnostic.structuralValidationPassed=true;
        diagnostic.structuralValidationReason=QStringLiteral("结构点验收已禁用");
    }
    if (stageTimes) stageClock.stop();
    diagnostic.accepted=true;
    diagnostic.reason=QStringLiteral("通过修正范围和结构覆盖验收");
    return diagnostic;
}

QString classifyBlocking(const IcpDiagnostics &diagnostic);

bool eligibleForRelaxedRetry(const IcpDiagnostics &diagnostic, QString *skipReason = nullptr) {
    if (diagnostic.accepted || diagnostic.reason == QStringLiteral("已取消")) {
        if (skipReason) *skipReason = QStringLiteral("该对已接受或已取消");
        return false;
    }
    // Conservative pre-check before paying for a full second attempt: when both
    // directions hold less than half of the evidence the default rule already
    // needed, a wider correction budget cannot create correspondences that were
    // never observed.
    if (diagnostic.failureCode == QStringLiteral("structural_points_insufficient")) {
        const int sourceFloor = qMax(1, diagnostic.structuralSourceMatchedEvidenceFloor / 2);
        const int targetFloor = qMax(1, diagnostic.structuralTargetMatchedEvidenceFloor / 2);
        if (diagnostic.structuralSourceMatchedPoints < sourceFloor
            && diagnostic.structuralTargetMatchedPoints < targetFloor) {
            if (skipReason) *skipReason = QStringLiteral(
                "命中证据不足（%1/%2 与 %3/%4）且低于证据下限一半，放宽重试无法补出对应点")
                .arg(diagnostic.structuralSourceMatchedPoints)
                .arg(diagnostic.structuralSourceMatchedEvidenceFloor)
                .arg(diagnostic.structuralTargetMatchedPoints)
                .arg(diagnostic.structuralTargetMatchedEvidenceFloor);
            return false;
        }
    }
    // Retry only well-defined geometric recovery cases (safety boundary,
    // pre-alignment boundary or structural validation). no_overlap, user
    // cancellation and obvious input errors are never retried.
    const QString blocking = classifyBlocking(diagnostic);
    const bool eligible = blocking == QStringLiteral("safety_boundary")
        || blocking == QStringLiteral("prealignment_boundary")
        || blocking == QStringLiteral("structural_validation");
    if (!eligible && skipReason)
        *skipReason = QStringLiteral("阻塞类别 %1 不在放宽重试范围").arg(blocking);
    return eligible;
}

QString classifyBlocking(const IcpDiagnostics &diagnostic)
{
    if (diagnostic.accepted) return QStringLiteral("accepted");
    if (diagnostic.reason == QStringLiteral("已取消")) return QStringLiteral("cancelled");
    // Machine-readable codes win over the legacy message matching below; the
    // substring rules stay as a fallback for paths that do not set a code yet.
    if (!diagnostic.failureCode.isEmpty()) {
        if (diagnostic.failureCode == QStringLiteral("plane_tilt_exceeds_limit")
            || diagnostic.failureCode == QStringLiteral("steep_plane_feature_insufficient"))
            return diagnostic.failureCode;
        if (diagnostic.failureCode == QStringLiteral("structural_points_insufficient")
            || diagnostic.failureCode == QStringLiteral("structural_coverage_insufficient"))
            return QStringLiteral("structural_validation");
    }
    if (diagnostic.reason == QStringLiteral("plane_tilt_exceeds_limit")) return QStringLiteral("plane_tilt_exceeds_limit");
    if (diagnostic.reason.contains(QStringLiteral("steep_plane_feature_insufficient"))) return QStringLiteral("steep_plane_feature_insufficient");
    if (diagnostic.stoppedAtSafetyBoundary || diagnostic.safetyConstrainedStepReductions > 0
        || diagnostic.reason.contains(QStringLiteral("安全范围")))
        return QStringLiteral("safety_boundary");
    if (diagnostic.reason.contains(QStringLiteral("无实际三维重叠"))
        || diagnostic.reason.contains(QStringLiteral("包围盒不重叠")))
        return QStringLiteral("no_overlap");
    if (diagnostic.overlapConstrainedStepReductions > 0
        && diagnostic.reason.contains(QStringLiteral("滑出实际相邻帧重叠区")))
        return QStringLiteral("overlap_boundary");
    if (diagnostic.reason.contains(QStringLiteral("主平面预对齐"))
        || diagnostic.reason.contains(QStringLiteral("主平面法向平移")))
        return QStringLiteral("prealignment_boundary");
    if (diagnostic.reason.contains(QStringLiteral("结构点"))
        || diagnostic.reason.contains(QStringLiteral("覆盖率")))
        return QStringLiteral("structural_validation");
    if (diagnostic.completedLevels < 3 || diagnostic.reason.contains(QStringLiteral("ICP"))
        || diagnostic.reason.contains(QStringLiteral("有效对应")))
        return QStringLiteral("icp_not_converged");
    return QStringLiteral("unknown");
}

IcpOptions relaxedOptions(const IcpOptions &strictOptions, const IcpDiagnostics &diagnostic) {
    IcpOptions options = strictOptions;
    options.relaxedStructuralValidation = true;
    options.maximumCorrectionTranslation = qMin(50.0f,
        qMax(10.0f, 3.0f * qMax(diagnostic.initialTranslation, diagnostic.correctionTranslation)));
    options.maximumCorrectionAngleDegrees = qMin(3.0f,
        qMax(1.0f, 3.0f * qMax(diagnostic.initialAngleDegrees, diagnostic.correctionAngleDegrees)));
    options.maximumPlanePrealignmentTranslation = qMax(12.0f, strictOptions.maximumPlanePrealignmentTranslation);
    options.applyWideStructureCandidate = true;
    return options;
}

MultiFrameRegistrationResult::OverlapDiagnostic overlapDiagnostic(
    int cloudId, const QVector<Point3D> &moving, const QVector<Point3D> &target) {
    MultiFrameRegistrationResult::OverlapDiagnostic result;
    result.cloudId=cloudId;
    const Bounds a=cloudBounds(moving),b=cloudBounds(target);
    const float width=qMax(0.0f,qMin(a.maximum.x(),b.maximum.x())-qMax(a.minimum.x(),b.minimum.x()));
    const float height=qMax(0.0f,qMin(a.maximum.y(),b.maximum.y())-qMax(a.minimum.y(),b.minimum.y()));
    result.intersectionArea=width*height;
    result.movingArea=qMax(0.0f,a.maximum.x()-a.minimum.x())*qMax(0.0f,a.maximum.y()-a.minimum.y());
    const float targetArea=qMax(0.0f,b.maximum.x()-b.minimum.x())*qMax(0.0f,b.maximum.y()-b.minimum.y());
    result.unionArea=result.movingArea+targetArea-result.intersectionArea;
    result.movingCoverage=result.movingArea>0?result.intersectionArea/result.movingArea:0.0f;
    result.intersectionOverUnion=result.unionArea>0?result.intersectionArea/result.unionArea:0.0f;
    result.warning=result.movingCoverage<0.1f;
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// 2.5D height-map attempt of one adjacent pair
//
// Order of operations (section 5 of the iteration contract): build the height
// maps, search dx/dy, search a small dz/yaw window, run the restricted
// point-to-plane ICP from the candidate, then apply the height-map acceptance.
// The legacy ICP result stays available as a comparison and is never replaced
// by this attempt.
// ---------------------------------------------------------------------------

struct HeightMapPairResult {
    bool attempted = false;
    bool searchAccepted = false;
    bool accepted = false;
    QString rejectReason;
    QMatrix4x4 correction;
    IcpDiagnostics diagnostic;
    PairStageTimes stageTimes;
    HeightMapSearchDiagnostic search;
    HeightMapTrajectoryDiagnostic trajectory;
    float gridResolutionMm = 0.0f;
    int width = 0;
    int height = 0;
};

HeightMapPairResult runHeightMapPair(const RobotBaseFrame &targetFrame,
                                     const ConvertedCloud &targetCloud,
                                     const RobotBaseFrame &sourceFrame,
                                     const ConvertedCloud &sourceCloud,
                                     int sourceIndex,
                                     const QVector3D &stitchAxis,
                                     float preferredSourcePlaneHeight,
                                     float preferredTargetPlaneHeight,
                                     bool planeIdentityTracked,
                                     const IcpDiagnostics &legacyDiagnostic,
                                     const IcpOptions &options,
                                     const ProgressCallback &progress)
{
    HeightMapPairResult result;
    result.attempted = true;
    result.correction.setToIdentity();
    const auto stampIdentity = [&](IcpDiagnostics *diagnostic) {
        diagnostic->attempted = true;
        diagnostic->targetCloudIndex = sourceIndex - 1;
        diagnostic->sourceCloudIndex = sourceIndex;
        diagnostic->heightMapAttempted = true;
        if (diagnostic->selectedRegistrationSource.isEmpty())
            diagnostic->selectedRegistrationSource = QStringLiteral("none");
    };
    const auto stampMetrics = [&](IcpDiagnostics *diagnostic) {
        diagnostic->heightMapGridResolutionMm = result.gridResolutionMm;
        diagnostic->heightMapWidth = result.width;
        diagnostic->heightMapHeight = result.height;
        diagnostic->heightMapScoreBefore = result.search.scoreBefore;
        diagnostic->heightMapScoreAfter = result.search.scoreAfter;
        diagnostic->heightMapRelativeImprovement = result.search.relativeImprovement;
        diagnostic->heightMapSourceValidRatio = result.search.sourceValidRatio;
        diagnostic->heightMapTargetValidRatio = result.search.targetValidRatio;
        diagnostic->heightMapSourceValidPoints = result.search.sourceValidPoints;
        diagnostic->heightMapTargetValidPoints = result.search.targetValidPoints;
        diagnostic->heightMapTranslationX = result.search.translationX;
        diagnostic->heightMapTranslationY = result.search.translationY;
        diagnostic->heightMapTranslationZ = result.search.translationZ;
        diagnostic->heightMapYawDegrees = result.search.yawDegrees;
        diagnostic->heightMapNormalizedCorrelation = result.search.correlationAfter;
        diagnostic->trajectoryDiagnosticReason = result.trajectory.reason;
    };
    const auto fail = [&](const QString &reason, bool replaceDiagnosticReason) {
        result.accepted = false;
        result.rejectReason = reason;
        result.diagnostic.heightMapAttempted = true;
        result.diagnostic.heightMapAccepted = false;
        result.diagnostic.heightMapRejectReason = reason;
        if (replaceDiagnosticReason) {
            result.diagnostic.reason = reason;
            result.diagnostic.failureCode = reason;
        }
        stampIdentity(&result.diagnostic);
        stampMetrics(&result.diagnostic);
    };
    result.diagnostic.heightMapAttempted = true;
    result.diagnostic.selectedRegistrationSource = QStringLiteral("none");

    // 1. Trajectory diagnostic on the existing data only.
    HeightMapTrajectoryInput targetInput;
    targetInput.points = &targetCloud.full;
    targetInput.scanRatios = &targetCloud.scanRatios;
    targetInput.startBaseFromFlange = targetFrame.startBaseFromFlange;
    targetInput.endBaseFromFlange = targetFrame.endBaseFromFlange;
    targetInput.dominantTravelAxis = targetFrame.dominantTravelAxis;
    targetInput.signedTravel = targetFrame.signedTravel;
    HeightMapTrajectoryInput sourceInput;
    sourceInput.points = &sourceCloud.full;
    sourceInput.scanRatios = &sourceCloud.scanRatios;
    sourceInput.startBaseFromFlange = sourceFrame.startBaseFromFlange;
    sourceInput.endBaseFromFlange = sourceFrame.endBaseFromFlange;
    sourceInput.dominantTravelAxis = sourceFrame.dominantTravelAxis;
    sourceInput.signedTravel = sourceFrame.signedTravel;
    result.trajectory = diagnoseHeightMapTrajectory(targetInput, sourceInput);
    if (!result.trajectory.ok) {
        fail(QStringLiteral("trajectory_diagnostic_failed"), true);
        return result;
    }

    // 2. Region: the robot-initial overlap box, i.e. the X/Y intersection of
    // the two frames. The joint bounding box of both frames is never used.
    const Bounds sourceBounds = cloudBounds(sourceCloud.full);
    const Bounds targetBounds = cloudBounds(targetCloud.full);
    HeightMapRegion region;
    region.minimumX = qMax(sourceBounds.minimum.x(), targetBounds.minimum.x());
    region.minimumY = qMax(sourceBounds.minimum.y(), targetBounds.minimum.y());
    region.maximumX = qMin(sourceBounds.maximum.x(), targetBounds.maximum.x());
    region.maximumY = qMin(sourceBounds.maximum.y(), targetBounds.maximum.y());
    if (!region.valid()) {
        fail(QStringLiteral("heightmap_region_invalid"), true);
        return result;
    }

    HeightMapOptions heightOptions;
    heightOptions.minimumGridResolutionMm = qMin(options.heightMapGridMinimumMm,
                                                 options.heightMapGridMaximumMm);
    heightOptions.maximumGridResolutionMm = qMax(options.heightMapGridMinimumMm,
                                                 options.heightMapGridMaximumMm);
    heightOptions.minimumValidRatio = options.heightMapMinimumValidRatio;
    heightOptions.minimumValidPoints = qMax(1, options.heightMapMinimumValidPoints);
    heightOptions.overlapMarginMm = options.overlapMargin;

    // 3. Grid resolution from the median point spacing of both frames.
    float sourceResolution = 0.0f;
    float targetResolution = 0.0f;
    QString resolutionError;
    if (!estimateHeightMapGridResolution(sourceCloud.full, heightOptions,
                                         &sourceResolution, &resolutionError)) {
        fail(resolutionError.isEmpty()
                 ? QStringLiteral("heightmap_grid_resolution_invalid") : resolutionError, true);
        return result;
    }
    if (!estimateHeightMapGridResolution(targetCloud.full, heightOptions,
                                         &targetResolution, &resolutionError)) {
        fail(resolutionError.isEmpty()
                 ? QStringLiteral("heightmap_grid_resolution_invalid") : resolutionError, true);
        return result;
    }
    result.gridResolutionMm = qBound(heightOptions.minimumGridResolutionMm,
                                     0.5f * (sourceResolution + targetResolution),
                                     heightOptions.maximumGridResolutionMm);

    // 4. One map per frame, on the same grid and over the same region.
    QElapsedTimer heightMapClock;
    heightMapClock.start();
    const HeightMap sourceMap = buildHeightMap(sourceCloud.full, region,
                                               result.gridResolutionMm, heightOptions);
    if (!sourceMap.ok) {
        fail(sourceMap.error.isEmpty() ? QStringLiteral("heightmap_insufficient_data")
                                       : sourceMap.error, true);
        return result;
    }
    const HeightMap targetMap = buildHeightMap(targetCloud.full, region,
                                               result.gridResolutionMm, heightOptions);
    if (!targetMap.ok) {
        fail(targetMap.error.isEmpty() ? QStringLiteral("heightmap_insufficient_data")
                                       : targetMap.error, true);
        return result;
    }
    result.width = targetMap.width;
    result.height = targetMap.height;

    // 5. Bounded coarse-to-fine search: stage I dx/dy, stage II a small
    // dz/yaw window. The full 6 DoF is never opened.
    HeightMapSearchOptions searchOptions;
    searchOptions.searchEnabled = options.heightMapSearchEnabled;
    searchOptions.maximumTranslationMm = options.heightMapMaxTranslationMm;
    searchOptions.maximumYawDegrees = options.heightMapMaxYawDegrees;
    searchOptions.minimumRelativeImprovement = options.heightMapMinimumRelativeImprovement;
    searchOptions.gridResolutionMm = result.gridResolutionMm;
    searchOptions.overlapMarginMm = options.overlapMargin;
    searchOptions.isCancelled = [&options]() { return cancellationRequested(options); };
    result.search = searchHeightMapAlignment(sourceMap, targetMap, searchOptions);
    result.stageTimes.coarseSearchMs += heightMapClock.elapsed();
    stampIdentity(&result.diagnostic);
    stampMetrics(&result.diagnostic);
    if (result.search.reason == QStringLiteral("已取消")) {
        fail(QStringLiteral("已取消"), true);
        return result;
    }
    if (!result.search.accepted) {
        fail(result.search.reason.isEmpty() ? QStringLiteral("heightmap_search_failed")
                                            : result.search.reason, true);
        return result;
    }
    result.searchAccepted = true;
    // Height-map acceptance gates the search itself does not own.
    if (result.search.sourceValidRatio < options.heightMapMinimumValidRatio
        || result.search.targetValidRatio < options.heightMapMinimumValidRatio
        || result.search.sourceValidPoints < options.heightMapMinimumValidPoints
        || result.search.targetValidPoints < options.heightMapMinimumValidPoints) {
        fail(QStringLiteral("heightmap_insufficient_data"), true);
        return result;
    }
    if (cancellationRequested(options)) {
        fail(QStringLiteral("已取消"), true);
        return result;
    }

    // 6. Restricted point-to-plane ICP from the candidate. Plane pre-alignment,
    // the tangent coarse search and the wide structure search stay off, so the
    // candidate is only refined inside the strict budget.
    IcpOptions restrictedOptions = options;
    restrictedOptions.planePrealignmentEnabled = false;
    restrictedOptions.tangentCoarseAlignmentEnabled = false;
    restrictedOptions.tangentCoarseAlignmentAuto = false;
    restrictedOptions.wideStructureDiagnosticEnabled = false;
    restrictedOptions.applyWideStructureCandidate = false;
    restrictedOptions.structuralValidationEnabled = true;
    restrictedOptions.maximumCorrectionTranslation = options.heightMapMaxTranslationMm;
    restrictedOptions.maximumCorrectionAngleDegrees = options.heightMapMaxYawDegrees;
    RestrictedRegistration restricted;
    restricted.enabled = true;
    restricted.restrictedDof = options.heightMapRestrictedDof;
    restricted.maximumTranslationMm = options.heightMapMaxTranslationMm;
    restricted.maximumYawDegrees = options.heightMapMaxYawDegrees;
    restricted.initialCorrection = result.search.candidate;
    restricted.bypassLegacyStructuralAcceptance = true;
    QMatrix4x4 restrictedCorrection;
    result.diagnostic = registerPair(sourceCloud.sample, targetCloud.sample, restrictedOptions,
        sourceIndex, stitchAxis, preferredSourcePlaneHeight, preferredTargetPlaneHeight,
        planeIdentityTracked, &restrictedCorrection, progress, &result.stageTimes, nullptr,
        restricted);
    result.correction = restrictedCorrection;
    result.diagnostic.heightMapRestrictedIcpCompletedLevels = result.diagnostic.completedLevels;
    result.diagnostic.heightMapRestrictedIcpDegenerate = result.diagnostic.degenerate;
    stampIdentity(&result.diagnostic);
    stampMetrics(&result.diagnostic);
    if (result.diagnostic.reason == QStringLiteral("已取消")) {
        fail(QStringLiteral("已取消"), false);
        return result;
    }
    if (!result.diagnostic.accepted) {
        fail(result.diagnostic.failureCode.isEmpty()
                 ? QStringLiteral("heightmap_restricted_icp_failed")
                 : result.diagnostic.failureCode, false);
        return result;
    }

    // 7. 2.5D acceptance. It uses the height-map criteria of this iteration;
    // the legacy 3 mm structural coverage stays recorded but does not gate it.
    if (!result.diagnostic.converged || result.diagnostic.completedLevels != 3) {
        fail(QStringLiteral("heightmap_restricted_icp_incomplete"), false);
        return result;
    }
    if (result.diagnostic.degenerate) {
        fail(QStringLiteral("heightmap_restricted_icp_degenerate"), false);
        return result;
    }
    if (result.diagnostic.stoppedAtSafetyBoundary) {
        fail(QStringLiteral("heightmap_candidate_exceeds_budget"), false);
        return result;
    }
    if (result.diagnostic.actualOverlapSourceCount < qMax(100, options.minimumStructuralPoints)
        || result.diagnostic.actualOverlapTargetCount < qMax(100, options.minimumStructuralPoints)) {
        fail(QStringLiteral("heightmap_insufficient_overlap"), false);
        return result;
    }
    // The candidate must not destroy the real overlap the legacy baseline
    // found: a correction that keeps only a fragment of it never reaches the
    // formal output.
    if (legacyDiagnostic.actualOverlapSourceCount >= qMax(100, options.minimumStructuralPoints)
        && result.diagnostic.actualOverlapSourceCount * 2 < legacyDiagnostic.actualOverlapSourceCount) {
        fail(QStringLiteral("heightmap_candidate_overlap_destroyed"), false);
        return result;
    }
    if (legacyDiagnostic.actualOverlapTargetCount >= qMax(100, options.minimumStructuralPoints)
        && result.diagnostic.actualOverlapTargetCount * 2 < legacyDiagnostic.actualOverlapTargetCount) {
        fail(QStringLiteral("heightmap_candidate_overlap_destroyed"), false);
        return result;
    }
    if (result.diagnostic.correctionTranslation > options.heightMapMaxTranslationMm + 1.0e-4f
        || result.diagnostic.correctionAngleDegrees > options.heightMapMaxYawDegrees + 1.0e-6f) {
        fail(QStringLiteral("heightmap_candidate_exceeds_budget"), false);
        return result;
    }
    for (int element = 0; element < 16; ++element) {
        if (!std::isfinite(result.correction.constData()[element])) {
            fail(QStringLiteral("heightmap_non_finite_result"), false);
            return result;
        }
    }
    if (!std::isfinite(result.diagnostic.heightMapScoreAfter)
        || !std::isfinite(result.diagnostic.heightMapScoreBefore)
        || !std::isfinite(result.diagnostic.correctionTranslation)
        || !std::isfinite(result.diagnostic.correctionAngleDegrees)) {
        fail(QStringLiteral("heightmap_non_finite_result"), false);
        return result;
    }
    result.accepted = true;
    result.diagnostic.heightMapAccepted = true;
    result.diagnostic.selectedRegistrationSource = QStringLiteral("heightmap_restricted_icp");
    result.diagnostic.heightMapRejectReason.clear();
    result.diagnostic.reason = QStringLiteral("通过 2.5D 高程图验收（新口径：高程图有效比例/相对改善/受限 ICP 四自由度，非旧结构点 3 mm 验收）");
    result.diagnostic.failureCode.clear();
    stampIdentity(&result.diagnostic);
    stampMetrics(&result.diagnostic);
    return result;
}

MultiFrameRegistrationResult registerRobotBaseFrames(QVector<RobotBaseFrame> inputs,
                                                      const IcpOptions &icp,
                                                      const ProgressCallback &progress) {
    MultiFrameRegistrationResult result;
    const auto update=[&](float value,const QString &message){if(progress)progress(qBound(0.0f,value,1.0f),message);};
    const int minimumInputs=icp.enabled?2:1;
    if(inputs.size()<minimumInputs){
        result.error=icp.enabled?QStringLiteral("配准至少需要两个扫描点云")
                                :QStringLiteral("坐标转换至少需要一个扫描点云");
        return result;
    }
    if(icp.enabled&&(icp.voxelLevels.size()!=3||icp.correspondenceDistances.size()!=3)){
        result.error=QStringLiteral("参考流程要求三个体素和对应距离层级");return result;
    }
    QVector<ConvertedCloud> clouds;
    clouds.reserve(inputs.size());
    for(int index=0;index<inputs.size();++index){
        if(cancellationRequested(icp)){result.cancelled=true;result.error=QStringLiteral("已取消");return result;}
        update(0.15f*float(index)/float(inputs.size()),QStringLiteral("准备机器人基坐标点云 %1/%2：%3").arg(index+1).arg(inputs.size()).arg(inputs[index].sourceFile));
        if(inputs[index].fullPoints.isEmpty()||inputs[index].samplePoints.isEmpty()){
            result.error=QStringLiteral("第 %1 帧机器人基坐标点云为空").arg(index+1);return result;
        }
        if (inputs[index].fullPoints.size() != inputs[index].sourceIndices.size()
            || inputs[index].fullPoints.size() != inputs[index].scanRatios.size()) {
            result.error = QStringLiteral("第 %1 帧点云与来源映射长度不一致").arg(index + 1);
            return result;
        }
        ConvertedCloud converted;
        converted.full=std::move(inputs[index].fullPoints);
        converted.sample=std::move(inputs[index].samplePoints);
        converted.sourceIndices=std::move(inputs[index].sourceIndices);
        converted.scanRatios=std::move(inputs[index].scanRatios);
        converted.declaredCount=inputs[index].declaredCount;
        converted.rejectedBasic=inputs[index].rejectedInvalid;
        converted.rejectedRange=inputs[index].rejectedRange;
        converted.inputYMinimum=inputs[index].inputYMinimum;
        converted.inputYMaximum=inputs[index].inputYMaximum;
        FrameTransformMetadata frame;
        frame.sourceFile=inputs[index].sourceFile;
        frame.startBaseFromFlange=inputs[index].startBaseFromFlange;
        frame.endBaseFromFlange=inputs[index].endBaseFromFlange;
        frame.flangeFromDepth=inputs[index].flangeFromDepth;
        frame.declaredCount=converted.declaredCount;
        frame.convertedCount=converted.full.size();
        frame.rejectedInvalid=converted.rejectedBasic;
        frame.rejectedRange=converted.rejectedRange;
        frame.inputYMinimum=converted.inputYMinimum;
        frame.inputYMaximum=converted.inputYMaximum;
        frame.signedTravel=inputs[index].signedTravel;
        frame.dominantTravelAxis=inputs[index].dominantTravelAxis;
        result.frameMetadata.push_back(frame);
        result.diagnostics+=QStringLiteral("scan %1: robot_base kept=%2/%3, basic rejected=%4, range rejected=%5, sample=%6, PLY.Y=[%7,%8], base bounds=%9\n")
            .arg(index+1).arg(converted.full.size()).arg(converted.declaredCount)
            .arg(converted.rejectedBasic).arg(converted.rejectedRange).arg(converted.sample.size())
            .arg(converted.inputYMinimum,0,'g',8).arg(converted.inputYMaximum,0,'g',8)
            .arg(boundsText(cloudBounds(converted.full)));
        result.sourceFiles.push_back(inputs[index].sourceFile);
        clouds.push_back(std::move(converted));
    }
    result.registrationCorrections.resize(inputs.size());
    result.planeIdentityTrackingValid=trackPlaneIdentity(clouds,icp,&result.trackedPlaneHeights,
        &result.planeIdentityConsensusHeight,&result.planeIdentityDiagnostics);
    if (!result.planeIdentityTrackingValid) {
        QString steepReason;
        if (trackSteepPlaneIdentity(clouds, icp, &steepReason)) {
            result.planeIdentityTrackingValid = true;
            result.planeIdentityDiagnostics = steepReason;
            result.trackedPlaneHeights.fill(0.0f, clouds.size());
            result.planeIdentityConsensusHeight = 0.0f;
        } else if (!steepReason.isEmpty()) {
            result.planeIdentityDiagnostics += QStringLiteral("；%1").arg(steepReason);
        }
    }
    result.diagnostics+=QStringLiteral("Plane identity: %1\n").arg(result.planeIdentityDiagnostics);
    for(QMatrix4x4 &matrix:result.registrationCorrections)matrix.setToIdentity();
    result.registrationCorrections[0].setToIdentity();
    IcpDiagnostics reference;reference.filePath=inputs[0].sourceFile;reference.sourceCloudIndex=0;reference.accepted=true;reference.reason=QString::fromUtf8("参考点云");
    reference.blockingClass=QStringLiteral("accepted");
    result.icpDiagnostics.push_back(reference);
    if (icp.collectDiagnosticFrames) {
        QElapsedTimer frameClock;
        frameClock.start();
        result.diagnosticPreIcpFrames.reserve(clouds.size());
        for(const ConvertedCloud &cloud:clouds)
            result.diagnosticPreIcpFrames.push_back(voxelDownsample(cloud.sample,2.0f));
        result.diagnosticFrameMs += frameClock.elapsed();
    }
    if(!icp.enabled){
        for(int index=1;index<inputs.size();++index){
            IcpDiagnostics skipped;
            skipped.filePath=inputs[index].sourceFile;
            skipped.targetCloudIndex=index-1;
            skipped.sourceCloudIndex=index;
            skipped.accepted=true;
            skipped.reason=QStringLiteral("仅手眼坐标转换，未执行 ICP");
            skipped.blockingClass=QStringLiteral("accepted");
            result.icpDiagnostics.push_back(skipped);
        }
    }
    const int pairCount=inputs.size()-1;
    const int requestedParallelTasks = icp.maxParallelTasks;
    const int actualParallelTasks = icp.parallelEnabled
        ? (requestedParallelTasks > 0 ? requestedParallelTasks
                                      : qMax(1, qMin(4, QThread::idealThreadCount() - 1)))
        : 1;
    result.parallelEnabled = icp.parallelEnabled && actualParallelTasks > 1;
    result.requestedParallelTasks = requestedParallelTasks;
    result.actualParallelTasks = result.parallelEnabled ? actualParallelTasks : 1;
    // Read-only view of the converted clouds. Workers must never reach the
    // mutable container: a non-const clouds[index] would detach the shared
    // buffer while another worker is still reading it. Every worker returns a
    // private OverlapDiagnostic and the join loop below commits it in scan
    // order, so the overlap results stay order-independent.
    const QVector<ConvertedCloud> *const cloudView = &clouds;
    QVector<MultiFrameRegistrationResult::OverlapDiagnostic> preparedOverlaps(pairCount);
    if (icp.enabled && result.parallelEnabled) {
        QThreadPool pool;
        pool.setMaxThreadCount(actualParallelTasks);
        QVector<QFuture<MultiFrameRegistrationResult::OverlapDiagnostic>> futures;
        futures.reserve(pairCount);
        for (int index = 1; index < inputs.size(); ++index)
            futures.push_back(QtConcurrent::run(&pool, [cloudView, index] {
                return overlapDiagnostic(index, cloudView->at(index).sample,
                                         cloudView->at(index - 1).sample);
            }));
        // Explicit join barrier: no worker outlives this block.
        for (int index = 0; index < futures.size(); ++index)
            preparedOverlaps[index] = futures[index].result();
    } else if (icp.enabled) {
        for (int index = 1; index < inputs.size(); ++index)
            preparedOverlaps[index - 1] = overlapDiagnostic(index, cloudView->at(index).sample,
                                                            cloudView->at(index - 1).sample);
    }
    // The per-pair ICP loop stays strictly serial and in scan order: each
    // accepted correction is composed onto the previous frame's global
    // correction and applied to that frame's cloud, so parallelising these
    // commits would change the result. Only the read-only overlap
    // precomputation above runs in parallel.
    if(icp.enabled)
    for(int index=1;index<inputs.size();++index){
        const float pairStart=0.15f+(0.85f*float(index-1)/float(pairCount));
        const float pairSpan=0.85f/float(pairCount);
        update(pairStart,QString::fromUtf8("配准 scan %1 到相邻点云 scan %2").arg(index+1).arg(index));
        result.overlapDiagnostics.push_back(preparedOverlaps[index - 1]);
        const QVector3D sourceCenter((inputs[index].startBaseFromFlange(0,3)+inputs[index].endBaseFromFlange(0,3))*.5f,
                                     (inputs[index].startBaseFromFlange(1,3)+inputs[index].endBaseFromFlange(1,3))*.5f,
                                     (inputs[index].startBaseFromFlange(2,3)+inputs[index].endBaseFromFlange(2,3))*.5f);
        const QVector3D targetCenter((inputs[index-1].startBaseFromFlange(0,3)+inputs[index-1].endBaseFromFlange(0,3))*.5f,
                                     (inputs[index-1].startBaseFromFlange(1,3)+inputs[index-1].endBaseFromFlange(1,3))*.5f,
                                     (inputs[index-1].startBaseFromFlange(2,3)+inputs[index-1].endBaseFromFlange(2,3))*.5f);
        QMatrix4x4 correction;
        const QVector<Point3D> originalSample=clouds[index].sample;
        const auto accumulateStageTimes=[&](const PairStageTimes &times,qint64 totalMs){
            result.preprocessMs+=times.preprocessMs;
            result.coarseSearchMs+=times.coarseSearchMs;
            result.icpLevel1Ms+=times.levelMs[0];
            result.icpLevel2Ms+=times.levelMs[1];
            result.icpLevel3Ms+=times.levelMs[2];
            result.structuralValidationMs+=times.structuralValidationMs;
            result.targetLevelCacheMs+=times.targetLevelCacheMs;
            result.correspondenceMs+=times.correspondenceMs;
            result.boundaryCheckMs+=times.boundaryCheckMs;
            result.registrationTotalMs+=totalMs;
            ++result.registrationAttemptCount;
        };
        const auto applyDiagnosticTimes=[&](IcpDiagnostics *target,const PairStageTimes &times,qint64 totalMs,qint64 strictMs){
            target->preprocessMs=times.preprocessMs;
            target->coarseSearchMs=times.coarseSearchMs;
            target->icpLevel1Ms=times.levelMs[0];
            target->icpLevel2Ms=times.levelMs[1];
            target->icpLevel3Ms=times.levelMs[2];
            target->structuralValidationMs=times.structuralValidationMs;
            target->targetLevelCacheMs=times.targetLevelCacheMs;
            target->correspondenceMs=times.correspondenceMs;
            target->boundaryCheckMs=times.boundaryCheckMs;
            target->attemptTotalMs=totalMs;
            target->strictAttemptTotalMs=strictMs;
        };
        PairStageTimes strictTimes;
        PairPreprocessCache pairCache;
        QElapsedTimer attemptClock;
        attemptClock.start();
        IcpDiagnostics diagnostic=registerPair(originalSample,clouds[index-1].sample,icp,index,sourceCenter-targetCenter,
            result.trackedPlaneHeights.value(index),result.trackedPlaneHeights.value(index-1),
            result.planeIdentityTrackingValid,&correction,
            [&](float fraction,const QString &message){update(pairStart+fraction*pairSpan,message);},
            &strictTimes,&pairCache);
        const qint64 strictTotalMs=attemptClock.elapsed();
        accumulateStageTimes(strictTimes,strictTotalMs);
        applyDiagnosticTimes(&diagnostic,strictTimes,strictTotalMs,0);
        diagnostic.filePath=inputs[index].sourceFile;
        if(diagnostic.reason==QStringLiteral("已取消")){result.cancelled=true;result.error=diagnostic.reason;return result;}
        diagnostic.attemptCount=1;
        diagnostic.primaryAttemptReason=diagnostic.reason;
        diagnostic.blockingClass=classifyBlocking(diagnostic);
        QString retrySkipReason;
        if(icp.recoveryMode && eligibleForRelaxedRetry(diagnostic,&retrySkipReason)){
            if(cancellationRequested(icp)){result.cancelled=true;result.error=QStringLiteral("已取消");return result;}
            diagnostic.relaxedRetryAttempted=true;
            result.diagnostics+=QStringLiteral("scan %1 retry start: class=%2, reason=%3\n")
                .arg(index+1).arg(diagnostic.blockingClass, diagnostic.reason);
            const IcpOptions retryOptions=relaxedOptions(icp,diagnostic);
            PairStageTimes retryTimes;
            QMatrix4x4 retryCorrection;
            attemptClock.start();
            IcpDiagnostics retry=registerPair(originalSample,clouds[index-1].sample,retryOptions,index,sourceCenter-targetCenter,
                result.trackedPlaneHeights.value(index),result.trackedPlaneHeights.value(index-1),
                result.planeIdentityTrackingValid,&retryCorrection,
                [&](float fraction,const QString &message){update(pairStart+fraction*pairSpan,message+QStringLiteral("（放宽重试）"));},
                &retryTimes,&pairCache);
            const qint64 retryTotalMs=attemptClock.elapsed();
            accumulateStageTimes(retryTimes,retryTotalMs);
            result.retryMs+=retryTotalMs;
            applyDiagnosticTimes(&retry,retryTimes,retryTotalMs,strictTotalMs);
            diagnostic.attemptCount=2;
            diagnostic.relaxedRetryReason=retry.reason;
            retry.blockingClass=classifyBlocking(retry);
            const bool retryCoverageImproved=retry.coverageWithin5mm>diagnostic.coverageWithin5mm+1.0e-4f
                ||retry.structuralBidirectionalCoverage5mm>diagnostic.structuralBidirectionalCoverage5mm+1.0e-4f;
            const bool retryGuard=retry.accepted&&retry.converged&&retry.completedLevels==3
                &&retry.observableDof>=1&&!retry.stoppedAtSafetyBoundary
                &&retry.actualOverlapSourceCount>=100&&retry.actualOverlapTargetCount>=100
                &&retry.correctionTranslation<=retryOptions.maximumCorrectionTranslation
                &&retry.correctionAngleDegrees<=retryOptions.maximumCorrectionAngleDegrees
                &&retry.structuralValidationPassed&&retryCoverageImproved
                &&(diagnostic.observableDof<1||retry.observableDof>=diagnostic.observableDof);
            if(retryGuard){
                retry.filePath=inputs[index].sourceFile;
                retry.primaryAttemptReason=diagnostic.primaryAttemptReason;
                retry.attemptCount=2;
                retry.relaxedRetryAttempted=true;
                retry.relaxedRetryAccepted=true;
                retry.acceptedByRetry=true;
                diagnostic=retry;
                correction=retryCorrection;
            }else{
                diagnostic.relaxedRetryAccepted=false;
                diagnostic.acceptedByRetry=false;
            }
            result.diagnostics+=QStringLiteral("scan %1 retry end: accepted=%2, class=%3, reason=%4\n")
                .arg(index+1)
                .arg(diagnostic.relaxedRetryAccepted ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(diagnostic.relaxedRetryAccepted ? QStringLiteral("accepted") : classifyBlocking(retry))
                .arg(diagnostic.relaxedRetryReason);
        }else{
            diagnostic.relaxedRetrySkippedReason=retrySkipReason;
            if(!retrySkipReason.isEmpty())
                result.diagnostics+=QStringLiteral("scan %1 retry skipped: %2\n")
                    .arg(index+1).arg(retrySkipReason);
        }
        diagnostic.blockingClass=classifyBlocking(diagnostic);
        // ------------------------------------------------------------------
        // 2.5D height-map path (opt-in, default off).
        //
        // The legacy attempt above is kept as the baseline of this pair. The
        // height-map attempt runs next; when it passes its own acceptance the
        // formal output uses its restricted ICP result, and the legacy numbers
        // stay a comparison that never overwrites it.
        // ------------------------------------------------------------------
        if(icp.heightMapRegistrationEnabled&&diagnostic.reason!=QStringLiteral("已取消")){
            const IcpDiagnostics legacyDiagnostic=diagnostic;
            const QMatrix4x4 legacyCorrection=correction;
            QElapsedTimer heightMapClock;
            heightMapClock.start();
            HeightMapPairResult heightMap=runHeightMapPair(inputs.at(index-1),clouds.at(index-1),
                inputs.at(index),clouds.at(index),index,sourceCenter-targetCenter,
                result.trackedPlaneHeights.value(index),result.trackedPlaneHeights.value(index-1),
                result.planeIdentityTrackingValid,legacyDiagnostic,icp,
                [&](float fraction,const QString &message){
                    update(pairStart+fraction*pairSpan,message+QStringLiteral("（2.5D 高程图）"));});
            const qint64 heightMapTotalMs=heightMapClock.elapsed();
            accumulateStageTimes(heightMap.stageTimes,heightMapTotalMs);
            applyDiagnosticTimes(&heightMap.diagnostic,heightMap.stageTimes,heightMapTotalMs,0);
            heightMap.diagnostic.filePath=inputs.at(index).sourceFile;
            heightMap.diagnostic.legacyIcpComparisonAvailable=true;
            heightMap.diagnostic.legacyIcpComparisonCoverage3mm=legacyDiagnostic.structuralBidirectionalCoverage3mm;
            heightMap.diagnostic.legacyIcpComparisonP95=qMax(legacyDiagnostic.structuralSourceToTargetP95,
                                                            legacyDiagnostic.structuralTargetToSourceP95);
            heightMap.diagnostic.legacyIcpAccepted=legacyDiagnostic.accepted;
            heightMap.diagnostic.legacyIcpRejectReason=legacyDiagnostic.accepted?QString():legacyDiagnostic.reason;
            if(heightMap.diagnostic.reason==QStringLiteral("已取消")){
                result.cancelled=true;result.error=heightMap.diagnostic.reason;return result;
            }
            // Selection. Both paths pass -> the height-map result is the formal
            // output; only the legacy path passes -> fall back; neither -> none.
            if(heightMap.accepted){
                diagnostic=heightMap.diagnostic;
                correction=heightMap.correction;
                diagnostic.selectedRegistrationSource=QStringLiteral("heightmap_restricted_icp");
                diagnostic.heightMapFallbackToIcp=false;
            }else if(legacyDiagnostic.accepted){
                diagnostic=legacyDiagnostic;
                correction=legacyCorrection;
                diagnostic.selectedRegistrationSource=QStringLiteral("legacy_icp");
                diagnostic.heightMapAttempted=true;
                diagnostic.heightMapAccepted=false;
                diagnostic.heightMapFallbackToIcp=true;
                diagnostic.heightMapRejectReason=heightMap.rejectReason;
                diagnostic.heightMapGridResolutionMm=heightMap.diagnostic.heightMapGridResolutionMm;
                diagnostic.heightMapWidth=heightMap.diagnostic.heightMapWidth;
                diagnostic.heightMapHeight=heightMap.diagnostic.heightMapHeight;
                diagnostic.heightMapSourceValidRatio=heightMap.diagnostic.heightMapSourceValidRatio;
                diagnostic.heightMapTargetValidRatio=heightMap.diagnostic.heightMapTargetValidRatio;
                diagnostic.heightMapSourceValidPoints=heightMap.diagnostic.heightMapSourceValidPoints;
                diagnostic.heightMapTargetValidPoints=heightMap.diagnostic.heightMapTargetValidPoints;
                diagnostic.heightMapScoreBefore=heightMap.diagnostic.heightMapScoreBefore;
                diagnostic.heightMapScoreAfter=heightMap.diagnostic.heightMapScoreAfter;
                diagnostic.heightMapRelativeImprovement=heightMap.diagnostic.heightMapRelativeImprovement;
                diagnostic.heightMapTranslationX=heightMap.diagnostic.heightMapTranslationX;
                diagnostic.heightMapTranslationY=heightMap.diagnostic.heightMapTranslationY;
                diagnostic.heightMapTranslationZ=heightMap.diagnostic.heightMapTranslationZ;
                diagnostic.heightMapYawDegrees=heightMap.diagnostic.heightMapYawDegrees;
                diagnostic.heightMapRestrictedIcpCompletedLevels=heightMap.diagnostic.heightMapRestrictedIcpCompletedLevels;
                diagnostic.heightMapRestrictedIcpDegenerate=heightMap.diagnostic.heightMapRestrictedIcpDegenerate;
                diagnostic.heightMapNormalizedCorrelation=heightMap.diagnostic.heightMapNormalizedCorrelation;
                diagnostic.trajectoryDiagnosticReason=heightMap.diagnostic.trajectoryDiagnosticReason;
            }else{
                diagnostic=heightMap.diagnostic;
                correction.setToIdentity();
                diagnostic.accepted=false;
                diagnostic.selectedRegistrationSource=QStringLiteral("none");
                diagnostic.heightMapFallbackToIcp=false;
                if(diagnostic.heightMapRejectReason.isEmpty())
                    diagnostic.heightMapRejectReason=legacyDiagnostic.failureCode.isEmpty()
                        ?legacyDiagnostic.reason:legacyDiagnostic.failureCode;
                diagnostic.reason=QStringLiteral("2.5D 与旧 ICP 均未通过：2.5D=%1；旧 ICP=%2")
                    .arg(diagnostic.heightMapRejectReason,legacyDiagnostic.reason);
                diagnostic.failureCode=QStringLiteral("registration_paths_failed");
            }
            diagnostic.legacyIcpComparisonAvailable=true;
            diagnostic.legacyIcpComparisonCoverage3mm=legacyDiagnostic.structuralBidirectionalCoverage3mm;
            diagnostic.legacyIcpComparisonP95=qMax(legacyDiagnostic.structuralSourceToTargetP95,
                                                   legacyDiagnostic.structuralTargetToSourceP95);
            diagnostic.legacyIcpAccepted=legacyDiagnostic.accepted;
            diagnostic.legacyIcpRejectReason=legacyDiagnostic.accepted?QString():legacyDiagnostic.reason;
            diagnostic.heightMapAttempted=true;
            // Visibility hard gate: it applies to whichever path produced the
            // final result and it wins over every local accuracy score. The
            // hit-point coverage (cov3_of_matched) never bypasses it.
            const float finalVisibleRatio=qMin(diagnostic.structuralSourceMatchRatio,
                                               diagnostic.structuralTargetMatchRatio);
            if(diagnostic.accepted&&finalVisibleRatio<icp.heightMapMinimumValidRatio){
                diagnostic.visibilityHardRejected=true;
                diagnostic.accepted=false;
                diagnostic.selectedRegistrationSource=QStringLiteral("none");
                diagnostic.heightMapAccepted=false;
                diagnostic.failureCode=QStringLiteral("visibility_hard_rejected");
                diagnostic.reason=QStringLiteral(
                    "visibility_hard_rejected：最终有效可见比例 %1 低于 %2，低可见性硬拒绝（命中点内覆盖率不作为放行依据）")
                    .arg(QString::number(double(finalVisibleRatio),'f',4))
                    .arg(QString::number(double(icp.heightMapMinimumValidRatio),'f',4));
                correction.setToIdentity();
            }
            result.diagnostics+=QStringLiteral(
                "heightmap_report: heightmap_registration_enabled=%1 heightmap_registration_attempted=%2 "
                "heightmap_registration_accepted=%3 heightmap_fallback_to_legacy_icp=%4 heightmap_grid_resolution_mm=%5 "
                "heightmap_valid_ratio_source=%6 heightmap_valid_ratio_target=%7 heightmap_valid_points_source=%8 "
                "heightmap_valid_points_target=%9 heightmap_score_before=%10 heightmap_score_after=%11 "
                "heightmap_relative_improvement=%12 heightmap_delta_xyz_mm=%13,%14,%15 heightmap_delta_yaw_deg=%16 "
                "heightmap_restricted_icp_levels=%17 heightmap_restricted_icp_degenerate=%18 "
                "selected_registration_source=%19 heightmap_reject_reason=%20 visibility_hard_rejected=%21 "
                "legacy_icp_comparison=%22\n")
                .arg(QStringLiteral("true"))
                .arg(QStringLiteral("true"))
                .arg(diagnostic.heightMapAccepted?QStringLiteral("true"):QStringLiteral("false"))
                .arg(diagnostic.heightMapFallbackToIcp?QStringLiteral("true"):QStringLiteral("false"))
                .arg(QString::number(double(diagnostic.heightMapGridResolutionMm),'f',4))
                .arg(QString::number(double(diagnostic.heightMapSourceValidRatio),'f',4))
                .arg(QString::number(double(diagnostic.heightMapTargetValidRatio),'f',4))
                .arg(diagnostic.heightMapSourceValidPoints)
                .arg(diagnostic.heightMapTargetValidPoints)
                .arg(QString::number(double(diagnostic.heightMapScoreBefore),'f',5))
                .arg(QString::number(double(diagnostic.heightMapScoreAfter),'f',5))
                .arg(QString::number(double(diagnostic.heightMapRelativeImprovement),'f',5))
                .arg(QString::number(double(diagnostic.heightMapTranslationX),'f',4))
                .arg(QString::number(double(diagnostic.heightMapTranslationY),'f',4))
                .arg(QString::number(double(diagnostic.heightMapTranslationZ),'f',4))
                .arg(QString::number(double(diagnostic.heightMapYawDegrees),'f',4))
                .arg(diagnostic.heightMapRestrictedIcpCompletedLevels)
                .arg(diagnostic.heightMapRestrictedIcpDegenerate?QStringLiteral("true"):QStringLiteral("false"))
                .arg(diagnostic.selectedRegistrationSource)
                .arg(diagnostic.heightMapRejectReason)
                .arg(diagnostic.visibilityHardRejected?QStringLiteral("true"):QStringLiteral("false"))
                .arg(diagnostic.legacyIcpComparisonAvailable?QStringLiteral("true"):QStringLiteral("false"));
        }
        diagnostic.blockingClass=classifyBlocking(diagnostic);
        result.icpDiagnostics.push_back(diagnostic);
        // Module-owned, machine-readable acceptance line. It surfaces the new
        // matched-point accounting through the existing diagnostics string, so
        // the tool, the GUI diagnostics panel and the log files show it without
        // any application-side change.
        result.diagnostics+=QStringLiteral(
            "pair %1 metrics: matched=%2/%3(%4) matched_target=%5/%6(%7) cov3_of_matched=%8 cov5_of_matched=%9 evidence_floor=%10/%11 visibility=%12 rule=%13 accepted=%14 trim=%16 normal_rejected=%17 coarse_trimmed=%18->%19 corr_xyz=%20,%21,%22 corr_angle=%23 reason=%15\n")
            .arg(index+1)
            .arg(diagnostic.structuralSourceMatchedPoints).arg(diagnostic.structuralSourcePoints)
            .arg(QString::number(double(diagnostic.structuralSourceMatchRatio),'f',4))
            .arg(diagnostic.structuralTargetMatchedPoints).arg(diagnostic.structuralTargetPoints)
            .arg(QString::number(double(diagnostic.structuralTargetMatchRatio),'f',4))
            .arg(QString::number(double(diagnostic.structuralBidirectionalCoverage3mmOfMatched),'f',4))
            .arg(QString::number(double(diagnostic.structuralBidirectionalCoverage5mmOfMatched),'f',4))
            .arg(diagnostic.structuralSourceMatchedEvidenceFloor)
            .arg(diagnostic.structuralTargetMatchedEvidenceFloor)
            .arg(diagnostic.visibilityWarning?QStringLiteral("warn"):QStringLiteral("ok"))
            .arg(diagnostic.structuralCoverageMode.isEmpty()?QStringLiteral("legacy_total"):diagnostic.structuralCoverageMode)
            .arg(diagnostic.accepted?QStringLiteral("true"):QStringLiteral("false"))
            .arg(diagnostic.reason)
            .arg(QString::number(double(diagnostic.correspondenceTrimFractionUsed),'f',3))
            .arg(diagnostic.normalRejectedCorrespondences)
            .arg(QString::number(double(diagnostic.tangentCoarseScoreBefore),'f',4))
            .arg(QString::number(double(diagnostic.tangentCoarseScoreAfter),'f',4))
            .arg(QString::number(double(diagnostic.correctionX),'f',5))
            .arg(QString::number(double(diagnostic.correctionY),'f',5))
            .arg(QString::number(double(diagnostic.correctionZ),'f',5))
            .arg(QString::number(double(diagnostic.correctionAngleDegrees),'f',5));
        if(diagnostic.accepted){
            const QMatrix4x4 globalCorrection=result.registrationCorrections[index-1]*correction;
            result.registrationCorrections[index]=globalCorrection;
            applyTransform(&clouds[index].full,globalCorrection);
        }else{
            correction.setToIdentity();
            result.registrationCorrections[index]=correction;
            result.diagnostics+=QStringLiteral("scan %1 registration failed: %2\n").arg(index+1).arg(diagnostic.reason);
            continue;
        }
    }
    if (icp.collectDiagnosticFrames) {
        QElapsedTimer frameClock;
        frameClock.start();
        result.diagnosticPostIcpFrames.reserve(clouds.size());
        for(int index=0;index<clouds.size();++index){
            QVector<Point3D> preview=voxelDownsample(clouds[index].sample,2.0f);
            applyTransform(&preview,result.registrationCorrections[index]);
            result.diagnosticPostIcpFrames.push_back(std::move(preview));
        }
        result.diagnosticFrameMs += frameClock.elapsed();
    }
    qsizetype total=0;for(const ConvertedCloud &cloud:clouds)total+=cloud.full.size();
    result.points.reserve(total);result.cloudIds.reserve(total);result.sourceIndices.reserve(total);result.scanRatios.reserve(total);
    for(int cloudId=0;cloudId<clouds.size();++cloudId)for(int index=0;index<clouds[cloudId].full.size();++index){result.points.push_back(clouds[cloudId].full[index]);result.cloudIds.push_back(cloudId);result.sourceIndices.push_back(clouds[cloudId].sourceIndices[index]);result.scanRatios.push_back(clouds[cloudId].scanRatios.value(index,0.0f));}
    if (result.points.size() != result.cloudIds.size()
        || result.points.size() != result.sourceIndices.size()
        || result.points.size() != result.scanRatios.size()) {
        result.error = QStringLiteral("配准结果点云与来源映射长度不一致");
        result.points.clear();
        result.cloudIds.clear();
        result.sourceIndices.clear();
        result.scanRatios.clear();
        return result;
    }
    result.pointsBeforeCrossCloudFusion=result.points.size();
    result.ok=!result.points.isEmpty();
    update(1.0f,icp.enabled
        ?QStringLiteral("%1 帧坐标转换和相邻帧配准完成").arg(inputs.size())
        :QStringLiteral("%1 帧手眼坐标转换完成，未执行 ICP").arg(inputs.size()));
    return result;
}

} // namespace pointcloud
