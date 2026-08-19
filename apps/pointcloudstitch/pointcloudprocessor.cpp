#include "pointcloudprocessor.h"
#include <pcv/filtering/downsample.h>
#include <pcv/io/ply_reader.h>
#include "handeye_transform.h"

#include <QFile>
#include <QFileInfo>
#include <QQuaternion>
#include <QRegularExpression>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
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
};

struct RawCloud {
    QVector<Point3D> points;
    qsizetype declaredCount = 0;
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
    return bounds.valid
        && point.x()>=bounds.minimum.x()&&point.x()<=bounds.maximum.x()
        && point.y()>=bounds.minimum.y()&&point.y()<=bounds.maximum.y()
        && point.z()>=bounds.minimum.z()&&point.z()<=bounds.maximum.z();
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

bool parseAsciiVertex(const QByteArray &line, int propertyCount,
                      int xIndex, int yIndex, int zIndex, Point3D *point) {
    const char *cursor = line.constData();
    char *end = nullptr;
    float xyz[3]{};
    for (int column = 0; column < propertyCount; ++column) {
        while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (!*cursor) return false;
        const float value = std::strtof(cursor, &end);
        if (end == cursor) return false;
        if (column == xIndex) xyz[0] = float(value);
        if (column == yIndex) xyz[1] = float(value);
        if (column == zIndex) xyz[2] = float(value);
        cursor = end;
    }
    point->x = xyz[0]; point->y = xyz[1]; point->z = xyz[2];
    return true;
}

bool readAsciiPly(const QString &path, RawCloud *result, QString *error,
                  const std::function<bool()> &isCancelled) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.readLine().trimmed() != "ply") {
        if (error) *error = QStringLiteral("不是 PLY 文件");
        return false;
    }
    bool ascii = false;
    bool inVertex = false;
    bool ended = false;
    qsizetype vertexCount = -1;
    QStringList properties;
    while (!file.atEnd()) {
        const QString text = QString::fromLatin1(file.readLine().trimmed());
        const QStringList fields = text.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (fields.isEmpty()) continue;
        if (fields[0] == QStringLiteral("format"))
            ascii = fields.value(1) == QStringLiteral("ascii");
        else if (fields[0] == QStringLiteral("element")) {
            inVertex = fields.value(1) == QStringLiteral("vertex");
            if (inVertex) vertexCount = fields.value(2).toLongLong();
        } else if (inVertex && fields[0] == QStringLiteral("property")) {
            if (fields.value(1) == QStringLiteral("list")) {
                if (error) *error = QStringLiteral("顶点 list 属性不受支持");
                return false;
            }
            properties.push_back(fields.last().toLower());
        } else if (fields[0] == QStringLiteral("end_header")) {
            ended = true;
            break;
        }
    }
    const int xIndex = properties.indexOf(QStringLiteral("x"));
    const int yIndex = properties.indexOf(QStringLiteral("y"));
    const int zIndex = properties.indexOf(QStringLiteral("z"));
    if (!ascii || !ended || vertexCount < 0 || xIndex < 0 || yIndex < 0 || zIndex < 0) {
        if (error) *error = QStringLiteral("参考流程要求包含 x/y/z 的 ASCII PLY");
        return false;
    }
    result->declaredCount = vertexCount;
    result->points.reserve(vertexCount);
    std::array<char, 64 * 1024> lineBuffer{};
    for (qsizetype index = 0; index < vertexCount; ++index) {
        if ((index % 150000) == 0 && isCancelled && isCancelled()) {
            if (error) *error = QStringLiteral("已取消");
            return false;
        }
        const qint64 lineLength = file.readLine(lineBuffer.data(), lineBuffer.size());
        if (lineLength <= 0) {
            if (error) *error = QStringLiteral("第 %1 个顶点数据不完整").arg(index + 1);
            return false;
        }
        if (lineLength == lineBuffer.size() - 1
            && !file.atEnd()
            && lineBuffer[lineLength - 1] != '\n') {
            if (error) *error = QStringLiteral("第 %1 个顶点行超过 64 KiB").arg(index + 1);
            return false;
        }
        const QByteArray vertexLine = QByteArray::fromRawData(lineBuffer.data(), int(lineLength));
        Point3D point;
        if (!parseAsciiVertex(vertexLine, properties.size(),
                                                 xIndex, yIndex, zIndex, &point)) {
            if (error) *error = QStringLiteral("第 %1 个顶点数据不完整").arg(index + 1);
            return false;
        }
        result->points.push_back(point);
    }
    return true;
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

Increment estimateIncrement(const QVector<Point3D> &source,
                            const QVector<Point3D> &target,
                            const SpatialIndex &targetIndex,
                            float maximumDistance,
                            const Bounds &actualOverlap) {
    struct Pair { QVector3D source,target,normal; int targetIndex=-1; };
    QVector<Pair> pairs;
    const float limitSquared=maximumDistance*maximumDistance;
    for(const Point3D &point:source){if(!insideBounds(vectorOf(point),actualOverlap))continue;const GridKey key=gridKey(point,maximumDistance);float best=limitSquared;int selected=-1;for(qint64 z=-1;z<=1;++z)for(qint64 y=-1;y<=1;++y)for(qint64 x=-1;x<=1;++x){const auto found=targetIndex.cells.find({key.x+x,key.y+y,key.z+z});if(found==targetIndex.cells.end())continue;for(int candidate:found->second){if(!insideBounds(vectorOf(target[candidate]),actualOverlap))continue;const QVector3D difference=vectorOf(point)-vectorOf(target[candidate]);const float distance=difference.lengthSquared();if(distance<best){best=distance;selected=candidate;}}}if(selected>=0){const Point3D &reference=target[selected];const QVector3D normal(reference.nx,reference.ny,reference.nz);if(normal.lengthSquared()>1.0e-8f)pairs.push_back({vectorOf(point),vectorOf(reference),normal.normalized(),selected});}}
    Increment result;
    result.correspondences=pairs.size();
    if(pairs.size()<100)return result;
    std::unordered_set<int> unique;
    QVector3D sourceCentroid;
    for(const Pair &pair:pairs)sourceCentroid+=pair.source;
    sourceCentroid/=float(pairs.size());
    double squaredRadius=0.0;
    for(const Pair &pair:pairs)squaredRadius+=(pair.source-sourceCentroid).lengthSquared();
    const double rotationScale=qMax(1.0,std::sqrt(squaredRadius/double(pairs.size())));
    double normalMatrix[6][6]{},rhs[6]{},weightSum=0.0;
    for(const Pair &pair:pairs){unique.insert(pair.targetIndex);const double residual=QVector3D::dotProduct(pair.normal,pair.source-pair.target);const double normalized=std::abs(residual)/maximumDistance;if(normalized>=1.0)continue;const double weight=std::pow(1.0-normalized*normalized,2.0);const QVector3D rotation=QVector3D::crossProduct(pair.source-sourceCentroid,pair.normal)/float(rotationScale);const double jacobian[6]{rotation.x(),rotation.y(),rotation.z(),pair.normal.x(),pair.normal.y(),pair.normal.z()};for(int row=0;row<6;++row){rhs[row]-=weight*jacobian[row]*residual;for(int column=0;column<6;++column)normalMatrix[row][column]+=weight*jacobian[row]*jacobian[column];}weightSum+=weight;}
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
    result.degenerate=result.observableDof<6;
    result.conditionRatio=float(minimumKept/maximumEigenvalue);
    for(int index=0;index<6;++index)result.normalizedUpdate[size_t(index)]=solution[index];
    result.rotationCenter=sourceCentroid;
    result.rotationScale=rotationScale;
    const QVector3D rotationVector{float(solution[0]/rotationScale),float(solution[1]/rotationScale),float(solution[2]/rotationScale)};
    const float angle=rotationVector.length();
    QQuaternion quaternion;
    if(angle>1.0e-12f)quaternion=QQuaternion::fromAxisAndAngle(rotationVector/angle,qRadiansToDegrees(angle));
    result.matrix.setToIdentity();
    result.matrix.translate(sourceCentroid);result.matrix.rotate(quaternion);result.matrix.translate(-sourceCentroid);
    result.matrix(0,3)+=float(solution[3]);result.matrix(1,3)+=float(solution[4]);result.matrix(2,3)+=float(solution[5]);
    double squared=0.0,xySquared=0.0,zSquared=0.0;
    for(const Pair &pair:pairs){const QVector3D residual=result.matrix.map(pair.source)-pair.target;squared+=residual.lengthSquared();xySquared+=residual.x()*residual.x()+residual.y()*residual.y();zSquared+=residual.z()*residual.z();}
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

struct PlaneDiagnostic { bool valid=false; QVector3D normal,centroid; float offset=0,height=0; };
struct DistanceDiagnostic { float p50=0,p90=0,p95=0,within3=0,within5=0,within10=0; };

DistanceDiagnostic nearestDistanceDiagnostic(const QVector<Point3D> &source,
                                             const QVector<Point3D> &target) {
    DistanceDiagnostic result;
    const QVector<Point3D> moving=voxelDownsample(source,2.0f),reference=voxelDownsample(target,2.0f);
    if(moving.isEmpty()||reference.isEmpty())return result;
    constexpr float searchDistance=15.0f;const SpatialIndex index=buildIndex(reference,searchDistance);const float limit2=searchDistance*searchDistance;
    QVector<float> distances;distances.reserve(moving.size());int within3=0,within5=0,within10=0;
    for(const Point3D &point:moving){const GridKey key=gridKey(point,searchDistance);float best=limit2;bool found=false;
        for(qint64 z=-1;z<=1;++z)for(qint64 y=-1;y<=1;++y)for(qint64 x=-1;x<=1;++x){const auto cell=index.cells.find({key.x+x,key.y+y,key.z+z});if(cell==index.cells.end())continue;for(int candidate:cell->second){const float value=(vectorOf(point)-vectorOf(reference[candidate])).lengthSquared();if(value<best){best=value;found=true;}}}
        if(!found)continue;const float distance=std::sqrt(best);distances.push_back(distance);within3+=distance<=3.0f;within5+=distance<=5.0f;within10+=distance<=10.0f;}
    if(distances.isEmpty())return result;
    std::sort(distances.begin(),distances.end());const auto percentile=[&](float p){return distances[qBound(0,int(std::floor(p*float(distances.size()-1))),int(distances.size()-1))];};
    result.p50=percentile(.50f);result.p90=percentile(.90f);result.p95=percentile(.95f);const float count=float(moving.size());result.within3=within3/count;result.within5=within5/count;result.within10=within10/count;return result;
}

PlaneDiagnostic pcaPlane(const QVector<Point3D> &points,
                         float preferredHeight=std::numeric_limits<float>::quiet_NaN()) {
    PlaneDiagnostic result;
    const QVector<Point3D> sample=voxelDownsample(points,1.0f);
    if(sample.size()<100)return result;
    constexpr float heightBin=0.5f;
    std::unordered_map<qint64,int> histogram;
    histogram.reserve(size_t(sample.size()/4));
    qint64 dominantBin=0;int dominantCount=0;
    for(const Point3D &point:sample){const qint64 bin=qint64(std::floor(point.z/heightBin));const int count=++histogram[bin];if(count>dominantCount){dominantCount=count;dominantBin=bin;}}
    if(std::isfinite(preferredHeight)){
        float bestDifference=std::numeric_limits<float>::max();
        const int minimumPeakCount=qMax(100,dominantCount/10);
        for(const auto &entry:histogram)if(entry.second>=minimumPeakCount){const float height=(float(entry.first)+.5f)*heightBin;const float difference=std::abs(height-preferredHeight);if(difference<bestDifference){bestDifference=difference;dominantBin=entry.first;}}
    }
    const float dominantHeight=(float(dominantBin)+.5f)*heightBin;
    QVector<Point3D> planePoints;planePoints.reserve(dominantCount*4);
    for(const Point3D &point:sample)if(std::abs(point.z-dominantHeight)<=1.5f)planePoints.push_back(point);
    if(planePoints.size()<100)return result;
    QVector3D mean;
    for(const Point3D &point:planePoints)mean+=vectorOf(point);
    mean/=float(planePoints.size());
    double covariance[3][3]{};
    for(const Point3D &point:planePoints){const QVector3D d=vectorOf(point)-mean;const double v[3]{d.x(),d.y(),d.z()};for(int row=0;row<3;++row)for(int column=0;column<3;++column)covariance[row][column]+=v[row]*v[column];}
    smallestEigenvector(covariance,&result.normal);
    if(result.normal.lengthSquared()<=1.0e-8f||std::abs(result.normal.z())<.8f)return result;
    if(result.normal.z()<0)result.normal=-result.normal;
    result.centroid=mean;result.offset=QVector3D::dotProduct(mean,result.normal);result.height=dominantHeight;result.valid=true;return result;
}

void collectInitialPairDiagnostics(const QVector<Point3D> &sourceCrop,
                                   const QVector<Point3D> &targetCrop,
                                   const QVector3D &stitchAxis,
                                   IcpDiagnostics *diagnostic,
                                   PlaneDiagnostic *sourcePlaneOutput,
                                   PlaneDiagnostic *targetPlaneOutput) {
    if(!diagnostic)return;
    diagnostic->sourceCropCount=sourceCrop.size();diagnostic->targetCropCount=targetCrop.size();
    if(!sourceCrop.isEmpty()&&!targetCrop.isEmpty()&&stitchAxis.lengthSquared()>1.0e-8f){
        const QVector3D axis=stitchAxis.normalized();const auto a=projectionRange(sourceCrop,axis),b=projectionRange(targetCrop,axis);
        diagnostic->projectedOverlapWidth=qMax(0.0f,qMin(a.second,b.second)-qMax(a.first,b.first));
    }
    const DistanceDiagnostic distances=nearestDistanceDiagnostic(sourceCrop,targetCrop);
    diagnostic->nearestDistanceP50=distances.p50;diagnostic->nearestDistanceP90=distances.p90;diagnostic->nearestDistanceP95=distances.p95;diagnostic->coverageWithin3mm=distances.within3;diagnostic->coverageWithin5mm=distances.within5;diagnostic->coverageWithin10mm=distances.within10;
    PlaneDiagnostic sourcePlane=pcaPlane(sourceCrop);
    PlaneDiagnostic targetPlane=sourcePlane.valid?pcaPlane(targetCrop,sourcePlane.height):pcaPlane(targetCrop);
    if(sourcePlane.valid&&targetPlane.valid){if(QVector3D::dotProduct(sourcePlane.normal,targetPlane.normal)<0){targetPlane.normal=-targetPlane.normal;targetPlane.offset=-targetPlane.offset;}diagnostic->planeDiagnosticValid=true;diagnostic->planeNormalAngleDegrees=qRadiansToDegrees(std::acos(qBound(-1.0f,QVector3D::dotProduct(sourcePlane.normal,targetPlane.normal),1.0f)));diagnostic->sourcePlaneHeight=sourcePlane.height;diagnostic->targetPlaneHeight=targetPlane.height;diagnostic->planeOffsetDifference=sourcePlane.height-targetPlane.height;}
    if(sourcePlaneOutput)*sourcePlaneOutput=sourcePlane;
    if(targetPlaneOutput)*targetPlaneOutput=targetPlane;
}

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

IcpDiagnostics registerPair(QVector<Point3D> *source,
                            const QVector<Point3D> &target,
                            const IcpOptions &options,
                            int sourceIndex,
                            const QVector3D &stitchAxis,
                            QMatrix4x4 *correction,
                            const ProgressCallback &progress) {
    IcpDiagnostics diagnostic;
    diagnostic.attempted=true;
    diagnostic.targetCloudIndex=sourceIndex-1;
    diagnostic.sourceCloudIndex=sourceIndex;
    diagnostic.filePath=QStringLiteral("scan %1").arg(sourceIndex+1);
    correction->setToIdentity();
    const Bounds sourceBounds=cloudBounds(*source),targetBounds=cloudBounds(target);
    const QVector3D low(qMax(sourceBounds.minimum.x(),targetBounds.minimum.x())-options.overlapMargin,
                        qMax(sourceBounds.minimum.y(),targetBounds.minimum.y())-options.overlapMargin,
                        qMax(sourceBounds.minimum.z(),targetBounds.minimum.z())-options.overlapMargin);
    const QVector3D high(qMin(sourceBounds.maximum.x(),targetBounds.maximum.x())+options.overlapMargin,
                         qMin(sourceBounds.maximum.y(),targetBounds.maximum.y())+options.overlapMargin,
                         qMin(sourceBounds.maximum.z(),targetBounds.maximum.z())+options.overlapMargin);
    if(high.x()<=low.x()||high.y()<=low.y()||high.z()<=low.z()){
        diagnostic.reason=QStringLiteral("相邻点云包围盒不重叠");return diagnostic;
    }
    const QVector<Point3D> sourceCrop=cropCloud(*source,low,high);
    const QVector<Point3D> targetCrop=cropCloud(target,low,high);
    PlaneDiagnostic sourcePlane,targetPlane;
    collectInitialPairDiagnostics(sourceCrop,targetCrop,stitchAxis,&diagnostic,&sourcePlane,&targetPlane);
    if(sourceCrop.size()<100||targetCrop.size()<100){diagnostic.reason=QStringLiteral("重叠区抽样点少于 100");return diagnostic;}
    if(options.planePrealignmentEnabled){
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
    QVector<Point3D> overlapAlignedSource=sourceCrop;
    applyTransform(&overlapAlignedSource,overlapBaselineCorrection);
    QVector<Point3D> overlapAlignedFull=*source;
    applyTransform(&overlapAlignedFull,overlapBaselineCorrection);
    const Bounds alignedSourceBounds=cloudBounds(overlapAlignedFull);
    const Bounds alignedTargetBounds=cloudBounds(target);
    Bounds actualOverlap;
    actualOverlap.minimum=QVector3D(qMax(alignedSourceBounds.minimum.x(),alignedTargetBounds.minimum.x()),
                                    qMax(alignedSourceBounds.minimum.y(),alignedTargetBounds.minimum.y()),
                                    qMax(alignedSourceBounds.minimum.z(),alignedTargetBounds.minimum.z()));
    actualOverlap.maximum=QVector3D(qMin(alignedSourceBounds.maximum.x(),alignedTargetBounds.maximum.x()),
                                    qMin(alignedSourceBounds.maximum.y(),alignedTargetBounds.maximum.y()),
                                    qMin(alignedSourceBounds.maximum.z(),alignedTargetBounds.maximum.z()));
    actualOverlap.valid=actualOverlap.maximum.x()>actualOverlap.minimum.x()
        &&actualOverlap.maximum.y()>actualOverlap.minimum.y()
        &&actualOverlap.maximum.z()>actualOverlap.minimum.z();
    if(!actualOverlap.valid){diagnostic.reason=QStringLiteral("主平面预对齐后相邻帧无实际三维重叠区");return diagnostic;}
    diagnostic.actualOverlapSourceCount=countInsideBounds(overlapAlignedSource,actualOverlap);
    diagnostic.actualOverlapTargetCount=countInsideBounds(targetCrop,actualOverlap);
    if(diagnostic.actualOverlapSourceCount<100||diagnostic.actualOverlapTargetCount<100){
        diagnostic.reason=QStringLiteral("实际相邻帧重叠区任一侧抽样点少于 100");return diagnostic;
    }
    const int maximumIterations[3]{60,45,35};
    for(int level=0;level<3;++level){
        float previousFitness=-1.0f;
        float previousRmse=std::numeric_limits<float>::max();
        const float voxel=options.voxelLevels[level];
        const float distance=options.correspondenceDistances[level];
        QVector<Point3D> moving=voxelDownsample(sourceCrop,voxel);
        QVector<Point3D> reference=voxelDownsample(cropCloud(targetCrop,actualOverlap.minimum,actualOverlap.maximum),voxel);
        if(moving.size()<100||reference.size()<100){diagnostic.reason=QStringLiteral("%1 mm 层点数少于 100").arg(voxel);return diagnostic;}
        applyTransform(&moving,*correction);
        QVector<Point3D> overlapBaseline=voxelDownsample(sourceCrop,voxel);
        applyTransform(&overlapBaseline,overlapBaselineCorrection);
        const int baselineInside=countInsideBounds(overlapBaseline,actualOverlap);
        const int minimumInside=qMax(100,int(std::ceil(double(baselineInside)*diagnostic.minimumOverlapRetention)));
        if(baselineInside<100){diagnostic.reason=QStringLiteral("%1 mm 层实际重叠区源点少于 100").arg(voxel);return diagnostic;}
        estimateNormals(&reference,qMax(voxel*4.0f,distance*1.5f));
        const SpatialIndex index=buildIndex(reference,distance);
        Increment last;
        std::array<std::array<double,6>,6> lockedEigenvectors{};
        std::array<double,6> lockedEigenvalues{};
        bool degeneracyBasisLocked=false;
        for(int iteration=0;iteration<maximumIterations[level];++iteration){
            if(options.isCancelled&&options.isCancelled()){diagnostic.reason=QStringLiteral("已取消");return diagnostic;}
            last=estimateIncrement(moving,reference,index,distance,actualOverlap);
            if(!last.ok){diagnostic.reason=QStringLiteral("%1 mm 层有效对应少于 100").arg(voxel);return diagnostic;}
            if(!degeneracyBasisLocked){
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
            const float stepFactors[4]{1.0f,0.5f,0.25f,0.125f};
            for(float factor:stepFactors){
                const QMatrix4x4 candidate=scaledRigidIncrement(last.matrix,factor);
                QVector<Point3D> trial=moving;applyTransform(&trial,candidate);
                if(countInsideBounds(trial,actualOverlap)>=minimumInside){
                    acceptedIncrement=candidate;overlapStepAccepted=true;
                    if(factor<0.999f)++diagnostic.overlapConstrainedStepReductions;
                    break;
                }
            }
            if(!overlapStepAccepted){diagnostic.reason=QStringLiteral("ICP 增量会使源点滑出实际相邻帧重叠区");return diagnostic;}
            last.matrix=acceptedIncrement;
            applyTransform(&moving,acceptedIncrement);
            *correction=acceptedIncrement*(*correction);
            ++diagnostic.iterations;
            const float translation=translationLength(*correction);
            const float angle=rotationAngle(*correction);
            if(translation>options.maximumCorrectionTranslation||angle>options.maximumCorrectionAngleDegrees){diagnostic.correctionTranslation=translation;diagnostic.correctionAngleDegrees=angle;diagnostic.correctionX=(*correction)(0,3);diagnostic.correctionY=(*correction)(1,3);diagnostic.correctionZ=(*correction)(2,3);diagnostic.reason=QStringLiteral("ICP 修正超过 %1 mm / %2 deg 安全范围").arg(options.maximumCorrectionTranslation).arg(options.maximumCorrectionAngleDegrees);return diagnostic;}
            const float fitness=float(last.correspondences)/qMax(1,moving.size());
            const float fitnessChange=previousFitness<0?std::numeric_limits<float>::max():std::abs(fitness-previousFitness)/qMax(1.0e-12f,std::abs(previousFitness));
            const float rmseChange=std::isfinite(previousRmse)?std::abs(last.rmse-previousRmse)/qMax(1.0e-12f,std::abs(previousRmse)):std::numeric_limits<float>::max();
            previousFitness=fitness;previousRmse=last.rmse;
            if(fitnessChange<=1.0e-7f&&rmseChange<=1.0e-7f){diagnostic.converged=true;break;}
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
        if(progress)progress(float(level+1)/3.0f,QStringLiteral("scan %1：%2 mm Point-to-Plane ICP").arg(sourceIndex+1).arg(voxel));
    }
    diagnostic.converged=true;
    diagnostic.accepted=true;
    diagnostic.correctionTranslation=translationLength(*correction);
    diagnostic.correctionAngleDegrees=rotationAngle(*correction);
    diagnostic.correctionX=(*correction)(0,3);diagnostic.correctionY=(*correction)(1,3);diagnostic.correctionZ=(*correction)(2,3);
    diagnostic.overlapRatio=diagnostic.fitness;
    diagnostic.reason=QStringLiteral("通过参考流程修正范围验收");
    return diagnostic;
}

WorldCloudMergeResult::OverlapDiagnostic overlapDiagnostic(
    int cloudId, const QVector<Point3D> &moving, const QVector<Point3D> &target) {
    WorldCloudMergeResult::OverlapDiagnostic result;
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

WorldCloudMergeResult mergePlyCloudsInWorld(const QVector<WorldCloudInput> &inputs,
                                             const IcpOptions &icp,
                                             const ProgressCallback &progress) {
    WorldCloudMergeResult result;
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
    const float conversionEnd=0.55f;
    for(int index=0;index<inputs.size();++index){
        if(icp.isCancelled&&icp.isCancelled()){result.cancelled=true;result.error=QStringLiteral("已取消");return result;}
        update(conversionEnd*float(index)/float(inputs.size()),QStringLiteral("读取并转换点云 %1/%2：%3").arg(index+1).arg(inputs.size()).arg(QFileInfo(inputs[index].filePath).fileName()));
        RawCloud raw;
        pcv::detail::io::PlyReadOptions readOptions;
        readOptions.isCancelled=icp.isCancelled;
        pcv::detail::io::PlyReadResult readResult=pcv::detail::io::readPly(inputs[index].filePath,readOptions);
        if(!readResult.ok){result.cancelled=readResult.cancelled;result.error=QStringLiteral("%1：%2").arg(inputs[index].filePath,readResult.error);return result;}
        raw.declaredCount=readResult.declaredPointCount;
        raw.points.swap(readResult.points);
        HandEyeCalibration calibration;
        calibration.flangeFromDepth=inputs[index].flangeFromDepth;
        calibration.valid=true;
        CloudTransformOptions transformOptions;
        transformOptions.layout=DepthPointLayout::LineProfileXz;
        transformOptions.sampleStride=icp.sampleStride;
        transformOptions.interpolateRotation=true;
        transformOptions.isCancelled=icp.isCancelled;
        RobotCloudResult transformed=transformLineScanToRobotBase(
            raw.points,calibration,inputs[index].startBaseFromFlange,
            inputs[index].endBaseFromFlange,transformOptions);
        if(!transformed.ok){
            result.cancelled=transformed.cancelled;
            result.error=QStringLiteral("第 %1 帧：%2").arg(index+1).arg(transformed.error);
            return result;
        }
        ConvertedCloud converted;
        converted.full=std::move(transformed.points);
        converted.sample=std::move(transformed.samplePoints);
        converted.sourceIndices=std::move(transformed.sourceIndices);
        converted.scanRatios=std::move(transformed.scanRatios);
        converted.declaredCount=raw.declaredCount;
        converted.rejectedBasic=transformed.rejectedInvalid;
        converted.rejectedRange=transformed.rejectedRange;
        converted.inputYMinimum=transformed.inputYMinimum;
        converted.inputYMaximum=transformed.inputYMaximum;
        FrameTransformMetadata frame;
        frame.sourceFile=inputs[index].filePath;
        frame.startBaseFromFlange=inputs[index].startBaseFromFlange;
        frame.endBaseFromFlange=inputs[index].endBaseFromFlange;
        frame.flangeFromDepth=inputs[index].flangeFromDepth;
        frame.declaredCount=converted.declaredCount;
        frame.convertedCount=converted.full.size();
        frame.rejectedInvalid=converted.rejectedBasic;
        frame.rejectedRange=converted.rejectedRange;
        frame.inputYMinimum=converted.inputYMinimum;
        frame.inputYMaximum=converted.inputYMaximum;
        frame.signedTravel=transformed.signedTravel;
        frame.dominantTravelAxis=transformed.dominantTravelAxis;
        result.frameMetadata.push_back(frame);
        result.diagnostics+=QStringLiteral("scan %1: kept=%2/%3, basic rejected=%4, range rejected=%5, sample=%6, PLY.Y=[%7,%8], base bounds=%9\n")
            .arg(index+1).arg(converted.full.size()).arg(converted.declaredCount)
            .arg(converted.rejectedBasic).arg(converted.rejectedRange).arg(converted.sample.size())
            .arg(converted.inputYMinimum,0,'g',8).arg(converted.inputYMaximum,0,'g',8)
            .arg(boundsText(cloudBounds(converted.full)));
        result.sourceFiles.push_back(inputs[index].filePath);
        clouds.push_back(std::move(converted));
    }
    result.registrationCorrections.resize(inputs.size());
    for(QMatrix4x4 &matrix:result.registrationCorrections)matrix.setToIdentity();
    result.registrationCorrections[0].setToIdentity();
    IcpDiagnostics reference;reference.filePath=inputs[0].filePath;reference.sourceCloudIndex=0;reference.accepted=true;reference.reason=QStringLiteral("参考点云");
    result.icpDiagnostics.push_back(reference);
    if(!icp.enabled){
        for(int index=1;index<inputs.size();++index){
            IcpDiagnostics skipped;
            skipped.filePath=inputs[index].filePath;
            skipped.targetCloudIndex=index-1;
            skipped.sourceCloudIndex=index;
            skipped.accepted=true;
            skipped.reason=QStringLiteral("仅手眼坐标转换，未执行 ICP");
            result.icpDiagnostics.push_back(skipped);
        }
    }
    const int pairCount=inputs.size()-1;
    if(icp.enabled)
    for(int index=1;index<inputs.size();++index){
        const float pairStart=conversionEnd+(0.43f*float(index-1)/float(pairCount));
        const float pairSpan=0.43f/float(pairCount);
        update(pairStart,QStringLiteral("配准 scan %1 到相邻点云 scan %2").arg(index+1).arg(index));
        result.overlapDiagnostics.push_back(overlapDiagnostic(index,clouds[index].sample,clouds[index-1].sample));
        const QVector3D sourceCenter((inputs[index].startBaseFromFlange(0,3)+inputs[index].endBaseFromFlange(0,3))*.5f,
                                     (inputs[index].startBaseFromFlange(1,3)+inputs[index].endBaseFromFlange(1,3))*.5f,
                                     (inputs[index].startBaseFromFlange(2,3)+inputs[index].endBaseFromFlange(2,3))*.5f);
        const QVector3D targetCenter((inputs[index-1].startBaseFromFlange(0,3)+inputs[index-1].endBaseFromFlange(0,3))*.5f,
                                     (inputs[index-1].startBaseFromFlange(1,3)+inputs[index-1].endBaseFromFlange(1,3))*.5f,
                                     (inputs[index-1].startBaseFromFlange(2,3)+inputs[index-1].endBaseFromFlange(2,3))*.5f);
        QMatrix4x4 correction;
        IcpDiagnostics diagnostic=registerPair(&clouds[index].sample,clouds[index-1].sample,icp,index,sourceCenter-targetCenter,&correction,
            [&](float fraction,const QString &message){update(pairStart+fraction*pairSpan,message);});
        diagnostic.filePath=inputs[index].filePath;
        result.icpDiagnostics.push_back(diagnostic);
        if(diagnostic.reason==QStringLiteral("已取消")){result.cancelled=true;result.error=diagnostic.reason;return result;}
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
    qsizetype total=0;for(const ConvertedCloud &cloud:clouds)total+=cloud.full.size();
    result.points.reserve(total);result.cloudIds.reserve(total);result.sourceIndices.reserve(total);result.scanRatios.reserve(total);
    for(int cloudId=0;cloudId<clouds.size();++cloudId)for(int index=0;index<clouds[cloudId].full.size();++index){result.points.push_back(clouds[cloudId].full[index]);result.cloudIds.push_back(cloudId);result.sourceIndices.push_back(clouds[cloudId].sourceIndices[index]);result.scanRatios.push_back(clouds[cloudId].scanRatios.value(index,0.0f));}
    result.pointsBeforeCrossCloudFusion=result.points.size();
    result.ok=!result.points.isEmpty();
    update(1.0f,icp.enabled
        ?QStringLiteral("%1 帧坐标转换和相邻帧配准完成").arg(inputs.size())
        :QStringLiteral("%1 帧手眼坐标转换完成，未执行 ICP").arg(inputs.size()));
    return result;
}

} // namespace pointcloud
