#include "pointcloudprocessor.h"
#include <pcv/io/cloud_cache.h>
#include <pcv/filtering/downsample.h>
#include <pcv/io/ply_reader.h>
#include <pcv/registration/handeye_transform.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QThreadPool>
#include <QQuaternion>
#include <QStringList>
#include <QVector3D>
#include <QtConcurrent>
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
    float signedTravel = 0.0f;
    int dominantTravelAxis = -1;
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
    qint64 readTotalMs = 0;
    qint64 transformTotalMs = 0;
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
    const float conversionEnd = 0.55f;
    QVector<ConvertedCloud> clouds(inputs.size());
    const int requestedParallelTasks = icp.maxParallelTasks;
    const int actualParallelTasks = icp.parallelEnabled
        ? (requestedParallelTasks > 0 ? requestedParallelTasks
                                      : qMax(1, qMin(4, QThread::idealThreadCount() - 1)))
        : 1;
    const bool useParallelFrames = icp.parallelEnabled && actualParallelTasks > 1;
    struct FrameTaskResult {
        ConvertedCloud cloud;
        FrameTransformMetadata metadata;
        QString error;
        bool ok = false;
        bool cancelled = false;
        bool hardFailure = false;
        qint64 readMs = 0;
        qint64 transformMs = 0;
    };
    const auto processFrame = [&](int index) {
        FrameTaskResult task;
        const auto &input = inputs[index];
        QElapsedTimer conversionClock;
        RawCloud raw;
        QString readError;
        conversionClock.start();
        const bool readOk = readCanonicalPly(input.filePath, &raw, &readError, icp.isCancelled);
        task.readMs = conversionClock.elapsed();
        if (!readOk) {
            task.cancelled = readError == QStringLiteral("已取消");
            task.hardFailure = !task.cancelled;
            task.error = QStringLiteral("%1：%2").arg(input.filePath, readError);
            if (task.hardFailure && icp.cancellation)
                icp.cancellation->store(true, std::memory_order_relaxed);
            return task;
        }
        conversionClock.restart();
        HandEyeCalibration calibration;
        calibration.flangeFromDepth = input.flangeFromDepth;
        calibration.valid = true;
        CloudTransformOptions transformOptions;
        transformOptions.layout = DepthPointLayout::LineProfileXz;
        transformOptions.sampleStride = icp.sampleStride;
        transformOptions.interpolateRotation = true;
        transformOptions.isCancelled = icp.isCancelled;
        RobotCloudResult transformed = transformLineScanToRobotBase(
            raw.points, calibration, input.startBaseFromFlange,
            input.endBaseFromFlange, transformOptions);
        task.transformMs = conversionClock.elapsed();
        if (!transformed.ok) {
            task.cancelled = transformed.cancelled;
            task.hardFailure = !transformed.cancelled;
            task.error = QStringLiteral("第 %1 帧：%2").arg(index + 1).arg(transformed.error);
            if (task.hardFailure && icp.cancellation)
                icp.cancellation->store(true, std::memory_order_relaxed);
            return task;
        }
        task.cloud.full = std::move(transformed.points);
        task.cloud.sample = std::move(transformed.samplePoints);
        task.cloud.sourceIndices = std::move(transformed.sourceIndices);
        task.cloud.scanRatios = std::move(transformed.scanRatios);
        task.cloud.declaredCount = raw.declaredCount;
        task.cloud.rejectedBasic = transformed.rejectedInvalid;
        task.cloud.rejectedRange = transformed.rejectedRange;
        task.cloud.inputYMinimum = transformed.inputYMinimum;
        task.cloud.inputYMaximum = transformed.inputYMaximum;
        task.cloud.signedTravel = transformed.signedTravel;
        task.cloud.dominantTravelAxis = transformed.dominantTravelAxis;
        task.metadata.sourceFile = input.filePath;
        task.metadata.startBaseFromFlange = input.startBaseFromFlange;
        task.metadata.endBaseFromFlange = input.endBaseFromFlange;
        task.metadata.flangeFromDepth = input.flangeFromDepth;
        task.metadata.declaredCount = task.cloud.declaredCount;
        task.metadata.convertedCount = task.cloud.full.size();
        task.metadata.rejectedInvalid = task.cloud.rejectedBasic;
        task.metadata.rejectedRange = task.cloud.rejectedRange;
        task.metadata.inputYMinimum = task.cloud.inputYMinimum;
        task.metadata.inputYMaximum = task.cloud.inputYMaximum;
        task.metadata.signedTravel = task.cloud.signedTravel;
        task.metadata.dominantTravelAxis = task.cloud.dominantTravelAxis;
        task.ok = true;
        return task;
    };
    QVector<FrameTaskResult> frameTasks(inputs.size());
    QElapsedTimer frameWallClock;
    frameWallClock.start();
    if (useParallelFrames) {
        QThreadPool pool;
        pool.setMaxThreadCount(actualParallelTasks);
        QVector<QFuture<FrameTaskResult>> futures;
        futures.reserve(inputs.size());
        for (int index = 0; index < inputs.size(); ++index)
            futures.push_back(QtConcurrent::run(&pool, processFrame, index));
        for (int index = 0; index < futures.size(); ++index)
            frameTasks[index] = futures[index].result();
    } else {
        for (int index = 0; index < frameTasks.size(); ++index)
            frameTasks[index] = processFrame(index);
    }
    const qint64 frameWallMs = frameWallClock.elapsed();
    int firstHardFailure = -1;
    for (int index = 0; index < frameTasks.size(); ++index) {
        if (frameTasks[index].hardFailure) { firstHardFailure = index; break; }
    }
    for (int index = 0; index < frameTasks.size(); ++index) {
        const FrameTaskResult &task = frameTasks[index];
        readTotalMs += task.readMs;
        transformTotalMs += task.transformMs;
        update(conversionEnd * float(index + 1) / float(inputs.size()),
               QStringLiteral("读取并转换点云 %1/%2：%3")
                   .arg(index + 1).arg(inputs.size())
                   .arg(QFileInfo(inputs[index].filePath).fileName()));
        if (index == firstHardFailure) {
            result.cancelled = task.cancelled;
            result.error = task.error;
            return result;
        }
        if (!task.ok && firstHardFailure < 0) {
            result.cancelled = true;
            result.error = QStringLiteral("已取消");
            return result;
        }
        clouds[index] = task.cloud;
        result.frameMetadata.push_back(task.metadata);
        result.diagnostics += QStringLiteral("scan %1: kept=%2/%3, basic rejected=%4, range rejected=%5, sample=%6, PLY.Y=[%7,%8], base bounds=%9, canonical binary reader\n")
            .arg(index + 1).arg(task.cloud.full.size()).arg(task.cloud.declaredCount)
            .arg(task.cloud.rejectedBasic).arg(task.cloud.rejectedRange).arg(task.cloud.sample.size())
            .arg(task.cloud.inputYMinimum, 0, 'g', 8).arg(task.cloud.inputYMaximum, 0, 'g', 8)
            .arg(boundsText(cloudBounds(task.cloud.full)));
        result.sourceFiles.push_back(inputs[index].filePath);
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
        frame.flangeFromDepth = inputs[index].flangeFromDepth;
        frame.declaredCount = clouds[index].declaredCount;
        frame.rejectedInvalid = clouds[index].rejectedBasic;
        frame.rejectedRange = clouds[index].rejectedRange;
        frame.inputYMinimum = clouds[index].inputYMinimum;
        frame.inputYMaximum = clouds[index].inputYMaximum;
        frame.signedTravel = clouds[index].signedTravel;
        frame.dominantTravelAxis = clouds[index].dominantTravelAxis;
        moduleFrames.push_back(std::move(frame));
    }
    WorldCloudMergeResult merged = pointcloud::registerRobotBaseFrames(std::move(moduleFrames), icp, progress);
    merged.readMs = readTotalMs;
    merged.transformMs = transformTotalMs;
    merged.conversionWallMs = frameWallMs;
    merged.readWorkMs = readTotalMs;
    merged.transformWorkMs = transformTotalMs;
    merged.parallelEnabled = useParallelFrames;
    merged.requestedParallelTasks = requestedParallelTasks;
    merged.actualParallelTasks = useParallelFrames ? actualParallelTasks : 1;
    merged.registrationWorkMs += readTotalMs + transformTotalMs;
    return merged;

}

} // namespace pointcloud
