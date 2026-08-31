#include "pointcloudprocessor.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/filtering/downsample.h>
#include <pcv/io/ply_reader.h>
#include <pcv/registration/handeye_transform.h>

#include <QFileInfo>
#include <QQuaternion>
#include <QStringList>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pointcloud {
namespace {

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
    qint64 boundaryScanElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 totalElapsedMs = 0;
    int readerWorkerCount = 0;
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

bool readCanonicalPly(const QString &path, RawCloud *result, QString *error,
                  const std::function<bool()> &isCancelled) {
    pcv::detail::io::PlyReadOptions options;
    options.isCancelled = isCancelled;
    pcv::detail::io::CachedCloudResult cached =
        pcv::detail::io::readPlyCached(path, {}, options);
    if (!cached.ok) {
        if (error) *error = cached.error;
        return false;
    }
    result->points = std::move(cached.points);
    result->declaredCount = result->points.size();
    result->readerWorkerCount = 0;
    return true;
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
        QString readError;
        if(!readCanonicalPly(inputs[index].filePath,&raw,&readError,icp.isCancelled)){
            result.cancelled=readError==QStringLiteral("已取消");
            result.error=QStringLiteral("%1：%2").arg(inputs[index].filePath,readError);
            return result;
        }
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
        result.diagnostics+=QStringLiteral("scan %1: kept=%2/%3, basic rejected=%4, range rejected=%5, sample=%6, PLY.Y=[%7,%8], base bounds=%9, canonical binary reader\n")
            .arg(index+1).arg(converted.full.size()).arg(converted.declaredCount)
            .arg(converted.rejectedBasic).arg(converted.rejectedRange).arg(converted.sample.size())
            .arg(converted.inputYMinimum,0,'g',8).arg(converted.inputYMaximum,0,'g',8)
            .arg(boundsText(cloudBounds(converted.full)))
            ;
        result.sourceFiles.push_back(inputs[index].filePath);
        clouds.push_back(std::move(converted));
    }

    // Registration and overlap handling are owned by module 40.  Keep the
    // conversion stage here because it reads application scan contracts;
    // hand the resulting robot-base frames to the shared pipeline so GUI and
    // interface callers cannot drift into a second ICP implementation.
    QVector<pointcloud::RobotBaseFrame> moduleFrames;
    moduleFrames.reserve(clouds.size());
    for (int index = 0; index < clouds.size(); ++index) {
        pointcloud::RobotBaseFrame frame;
        frame.sourceFile = inputs[index].filePath;
        frame.fullPoints = std::move(clouds[index].full);
        frame.samplePoints = std::move(clouds[index].sample);
        frame.sourceIndices = std::move(clouds[index].sourceIndices);
        frame.scanRatios = std::move(clouds[index].scanRatios);
        frame.startBaseFromFlange = inputs[index].startBaseFromFlange;
        frame.endBaseFromFlange = inputs[index].endBaseFromFlange;
        frame.declaredCount = clouds[index].declaredCount;
        frame.rejectedInvalid = clouds[index].rejectedBasic;
        frame.rejectedRange = clouds[index].rejectedRange;
        frame.inputYMinimum = clouds[index].inputYMinimum;
        frame.inputYMaximum = clouds[index].inputYMaximum;
        moduleFrames.push_back(std::move(frame));
    }
    return pointcloud::registerRobotBaseFrames(std::move(moduleFrames), icp, progress);

}

} // namespace pointcloud
