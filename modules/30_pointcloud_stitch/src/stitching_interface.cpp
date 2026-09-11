#include <pcv/interface/stitching_interface.h>

#include <pcv/io/cloud_cache.h>
#include <pcv/io/ply_writer.h>

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QTextStream>
#include <QtConcurrent>

#include <atomic>
#include <cmath>
#include <exception>
#include <memory>
#include <new>

namespace pcv::interface {
namespace {

StitchingResult failure(const QString &code, const QString &message)
{
    StitchingResult result;
    result.errorCode = code;
    result.message = message;
    return result;
}

bool finitePose(const pointcloud::RobotPose &pose)
{
    return std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z)
        && std::isfinite(pose.rx) && std::isfinite(pose.ry) && std::isfinite(pose.rz);
}

bool writeRobotBasePly(const QString &path, const QVector<pointcloud::Point3D> &points,
                       const std::function<bool()> &isCancelled, QString *error)
{
    pcv::detail::io::BinaryPlyWriteOptions options;
    options.includeNormals = false;
    options.comment = QStringLiteral("coordinate_frame robot_base");
    options.isCancelled = isCancelled;
    return pcv::detail::io::writeBinaryPly(path, points, error, options);
}

} // namespace

StitchingResult stitchRawLineProfilesImpl(const StitchingOptions &options)
{
    const auto cancelled = [&options]() {
        return options.isCancelled && options.isCancelled();
    };
    const auto progress = [&options](float value, const QString &message) {
        if (options.progress) options.progress(qBound(0.0f, value, 1.0f), message);
    };
    // Internal cancellation token: registration and seam hot loops read this
    // stable atomic instead of the caller's std::function. The token is
    // refreshed from the caller callback at stage boundaries and inside the
    // registration progress callback.
    qint64 readMsAccumulator = 0;
    qint64 transformMsAccumulator = 0;
    auto cancellationToken = std::make_shared<std::atomic_bool>(false);
    const auto syncCancellationToken = [&]() {
        if (cancelled()) cancellationToken->store(true, std::memory_order_relaxed);
    };
    const auto tokenCancelled = [cancellationToken]() {
        return cancellationToken->load(std::memory_order_relaxed);
    };
    if (options.frames.size() < 2)
        return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("配准至少需要两个 PLY 文件"));
    if (options.sampleStride < 1 || options.sampleStride > 100)
        return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("配准抽样步长必须在 1 到 100 之间"));
    if (options.calibrationPath.trimmed().isEmpty() || !QFileInfo::exists(options.calibrationPath))
        return failure(QStringLiteral("PCV_INPUT_001"), QStringLiteral("手眼标定 XML 不存在"));
    if (options.outputDirectory.trimmed().isEmpty())
        return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("输出目录不能为空"));
    if (!QDir().mkpath(options.outputDirectory))
        return failure(QStringLiteral("PCV_OUTPUT_001"), QStringLiteral("无法创建输出目录"));

    pointcloud::HandEyeCalibration calibration;
    qInfo() << "Stitching started, frames=" << options.frames.size()
            << "sampleStride=" << options.sampleStride;
    QString detail;
    if (!pointcloud::loadHandEyeCalibration(QFileInfo(options.calibrationPath).absoluteFilePath(),
                                            &calibration, &detail))
        return failure(QStringLiteral("PCV_TRANSFORM_001"), detail);

    QSet<QString> paths;
    QVector<pointcloud::RobotBaseFrame> frames(options.frames.size());
    QVector<QString> sourcePaths(options.frames.size());
    const int requestedParallelTasks = options.maxParallelTasks;
    const int actualParallelTasks = options.parallelEnabled
        ? (requestedParallelTasks > 0 ? requestedParallelTasks
                                      : qMax(1, qMin(4, QThread::idealThreadCount() - 1)))
        : 1;
    const bool useParallelFrames = options.parallelEnabled && actualParallelTasks > 1;
    for (int index = 0; index < options.frames.size(); ++index) {
        syncCancellationToken();
        if (tokenCancelled()) {
            StitchingResult result = failure(QString(), QStringLiteral("已取消"));
            result.cancelled = true;
            return result;
        }
        const StitchingFrameInput &input = options.frames[index];
        const QString sourcePath = QFileInfo(input.plyPath).absoluteFilePath();
        if (input.plyPath.trimmed().isEmpty() || !QFileInfo::exists(sourcePath))
            return failure(QStringLiteral("PCV_INPUT_001"), QStringLiteral("第 %1 帧 PLY 不存在").arg(index + 1));
        const QString cleanSourcePath = QDir::cleanPath(sourcePath);
        if (paths.contains(cleanSourcePath))
            return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("同一 PLY 不能重复添加：%1").arg(sourcePath));
        paths.insert(cleanSourcePath);
        if (!finitePose(input.startPose) || !finitePose(input.endPose))
            return failure(QStringLiteral("PCV_TRANSFORM_002"), QStringLiteral("第 %1 帧 Start/End 位姿包含无效数值").arg(index + 1));
        sourcePaths[index] = sourcePath;
    }

    struct FrameTaskResult {
        pointcloud::RobotBaseFrame frame;
        QString errorCode;
        QString error;
        qint64 readMs = 0;
        qint64 transformMs = 0;
        bool ok = false;
        bool cancelled = false;
        bool hardFailure = false;
    };
    const auto processFrame = [&](int index) {
        FrameTaskResult task;
        const StitchingFrameInput &input = options.frames[index];
        if (tokenCancelled()) {
            task.cancelled = true;
            task.error = QStringLiteral("已取消");
            return task;
        }
        QElapsedTimer stageClock;
        pcv::detail::io::PlyReadOptions readOptions;
        readOptions.isCancelled = tokenCancelled;
        stageClock.start();
        const auto cloud = pcv::detail::io::readPlyCached(sourcePaths[index], {}, readOptions);
        task.readMs = stageClock.elapsed();
        if (!cloud.ok) {
            task.cancelled = cloud.cancelled;
            task.hardFailure = !cloud.cancelled;
            task.errorCode = cloud.cancelled ? QString() : QStringLiteral("PCV_INPUT_002");
            task.error = cloud.error;
            if (task.hardFailure) cancellationToken->store(true, std::memory_order_relaxed);
            return task;
        }
        pointcloud::CloudTransformOptions transformOptions;
        transformOptions.layout = pointcloud::DepthPointLayout::LineProfileXz;
        transformOptions.sampleStride = options.sampleStride;
        transformOptions.isCancelled = tokenCancelled;
        stageClock.restart();
        auto transformed = pointcloud::transformLineScanToRobotBase(
            cloud.points, calibration, input.startPose, input.endPose, transformOptions);
        task.transformMs = stageClock.elapsed();
        if (!transformed.ok) {
            task.cancelled = transformed.cancelled;
            task.hardFailure = !transformed.cancelled;
            task.errorCode = transformed.cancelled ? QString() : QStringLiteral("PCV_TRANSFORM_001");
            task.error = transformed.error;
            if (task.hardFailure) cancellationToken->store(true, std::memory_order_relaxed);
            return task;
        }
        task.frame.sourceFile = sourcePaths[index];
        task.frame.fullPoints = std::move(transformed.points);
        task.frame.samplePoints = std::move(transformed.samplePoints);
        task.frame.sourceIndices = std::move(transformed.sourceIndices);
        task.frame.scanRatios = std::move(transformed.scanRatios);
        task.frame.startBaseFromFlange = pointcloud::robotPoseToMatrix(input.startPose);
        task.frame.endBaseFromFlange = pointcloud::robotPoseToMatrix(input.endPose);
        task.frame.flangeFromDepth = calibration.flangeFromDepth;
        task.frame.declaredCount = cloud.points.size();
        task.frame.rejectedInvalid = transformed.rejectedInvalid;
        task.frame.rejectedRange = transformed.rejectedRange;
        task.frame.inputYMinimum = transformed.inputYMinimum;
        task.frame.inputYMaximum = transformed.inputYMaximum;
        task.frame.signedTravel = transformed.signedTravel;
        task.frame.dominantTravelAxis = transformed.dominantTravelAxis;
        task.ok = true;
        return task;
    };
    QVector<FrameTaskResult> frameTasks(options.frames.size());
    QElapsedTimer frameWallClock;
    frameWallClock.start();
    if (useParallelFrames) {
        QThreadPool pool;
        pool.setMaxThreadCount(actualParallelTasks);
        QVector<QFuture<FrameTaskResult>> futures;
        futures.reserve(options.frames.size());
        for (int index = 0; index < options.frames.size(); ++index)
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
        readMsAccumulator += task.readMs;
        transformMsAccumulator += task.transformMs;
        progress(0.15f * float(index + 1) / float(options.frames.size()),
                 QStringLiteral("读取并转换点云 %1/%2：%3").arg(index + 1).arg(options.frames.size())
                     .arg(QFileInfo(sourcePaths[index]).fileName()));
        if (index == firstHardFailure) {
            StitchingResult result = failure(task.errorCode, task.error);
            return result;
        }
        if (!task.ok && firstHardFailure < 0) {
            StitchingResult result = failure(QString(), QStringLiteral("已取消"));
            result.cancelled = true;
            return result;
        }
        frames[index] = task.frame;
    }

    qInfo() << "Stitching hand-eye conversion completed, frames=" << frames.size();

    pointcloud::IcpOptions registrationOptions;
    registrationOptions.sampleStride = options.sampleStride;
    registrationOptions.collectDiagnosticFrames = options.collectDiagnosticFrames;
    registrationOptions.parallelEnabled = options.parallelEnabled;
    registrationOptions.maxParallelTasks = options.maxParallelTasks;
    registrationOptions.cancellation = cancellationToken;
    registrationOptions.isCancelled = tokenCancelled;
    QElapsedTimer registrationClock;
    registrationClock.start();
    auto registration = pointcloud::registerRobotBaseFrames(std::move(frames), registrationOptions,
        [&](float value, const QString &message) {
            syncCancellationToken();
            if (tokenCancelled()) return;
            progress(0.15f + value * 0.70f, message);
        });
    const qint64 registrationMs = registrationClock.elapsed();
    StitchingResult result;
    result.readMs = readMsAccumulator;
    result.transformMs = transformMsAccumulator;
    result.readWorkMs = readMsAccumulator;
    result.transformWorkMs = transformMsAccumulator;
    result.parallelEnabled = useParallelFrames;
    result.requestedParallelTasks = requestedParallelTasks;
    result.actualParallelTasks = useParallelFrames ? actualParallelTasks : 1;
    result.registrationMs = registrationMs;
    result.icpDiagnostics = registration.icpDiagnostics;
    result.diagnostics = registration.diagnostics;
    qInfo() << "Stitching ICP completed, ok=" << registration.ok
            << "points=" << registration.points.size();
    if (!registration.ok) {
        result.cancelled = registration.cancelled;
        result.errorCode = registration.cancelled ? QString() : QStringLiteral("PCV_STITCH_001");
        result.message = registration.error;
        return result;
    }
    for (int index = 1; index < registration.icpDiagnostics.size(); ++index) {
        if (!registration.icpDiagnostics[index].accepted) {
            result.errorCode = QStringLiteral("PCV_STITCH_001");
            result.message = QStringLiteral("第 %1 帧配准未通过：%2")
                .arg(index + 1).arg(registration.icpDiagnostics[index].reason);
            return result;
        }
    }
    if (options.seamEnabled) {
        pointcloud::SeamFusionOptions seamOptions;
        seamOptions.enabled = true;
        seamOptions.halfWidth = options.seamHalfWidthMm;
        seamOptions.cancellation = cancellationToken;
        seamOptions.isCancelled = cancelled;
        const auto seam = pointcloud::applyTrajectorySeamFusion(&registration, seamOptions);
        result.seamFusionMs = seam.seamFusionMs;
        result.seamDiagnostics = seam.diagnostics;
        if (!seam.ok) {
            result.errorCode = QStringLiteral("PCV_STITCH_001");
            result.message = seam.error;
            return result;
        }
        if (seam.cancelled) {
            result.cancelled = true;
            result.message = QStringLiteral("已取消");
            return result;
        }
        if (seam.diagnostics.size() != options.frames.size() - 1) {
            result.errorCode = QStringLiteral("PCV_STITCH_001");
            result.message = QStringLiteral("接缝融合未生成完整相邻帧诊断");
            return result;
        }
        for (const pointcloud::SeamFusionDiagnostic &diagnostic : seam.diagnostics) {
            // A pair with no real projected overlap is intentionally
            // fail-closed: keep its points and report the diagnostic. A
            // detected overlap that cannot produce a mutual blend remains a
            // hard stitching failure.
            if (!diagnostic.applied && diagnostic.actualOverlapValid) {
                result.errorCode = QStringLiteral("PCV_STITCH_001");
                result.message = QStringLiteral("接缝 %1-%2 未通过：%3")
                    .arg(diagnostic.cloudA + 1).arg(diagnostic.cloudB + 1).arg(diagnostic.reason);
                return result;
            }
        }
    }
    if (cancelled()) {
        result.cancelled = true;
        result.message = QStringLiteral("已取消");
        return result;
    }

    progress(0.90f, QStringLiteral("写入正式拼接点云"));
    const QString outputPath = QDir(QFileInfo(options.outputDirectory).absoluteFilePath())
        .filePath(QStringLiteral("stitched_robot_base.ply"));
    QElapsedTimer outputClock;
    outputClock.start();
    const bool outputWritten = writeRobotBasePly(outputPath, registration.points, cancelled, &detail);
    result.outputMs = outputClock.elapsed();
    if (!outputWritten) {
        result.cancelled = detail == QStringLiteral("已取消");
        result.errorCode = result.cancelled ? QString() : QStringLiteral("PCV_OUTPUT_002");
        result.message = detail;
        return result;
    }
    result.success = true;
    result.outputPly = outputPath;
    result.points = std::move(registration.points);
    result.diagnostics += QStringLiteral("formal_output=%1\n").arg(outputPath);
    result.totalMs = result.readMs + result.transformMs + result.registrationMs
        + result.seamFusionMs + result.outputMs;
    result.diagnostics += QStringLiteral(
        "timings_ms read=%1 transform=%2 registration=%3 pre=%4 coarse=%5 icp1=%6 icp2=%7 icp3=%8 structural=%9 retry=%10 attempts=%11 seam=%12 output=%13 total=%14\n")
        .arg(result.readMs).arg(result.transformMs).arg(result.registrationMs)
        .arg(registration.preprocessMs).arg(registration.coarseSearchMs)
        .arg(registration.icpLevel1Ms).arg(registration.icpLevel2Ms).arg(registration.icpLevel3Ms)
        .arg(registration.structuralValidationMs).arg(registration.retryMs)
        .arg(registration.registrationAttemptCount)
        .arg(result.seamFusionMs).arg(result.outputMs).arg(result.totalMs);
    progress(1.0f, QStringLiteral("点云配准完成"));
    return result;
}

StitchingResult stitchRawLineProfiles(const StitchingOptions &options)
{
    try {
        return stitchRawLineProfilesImpl(options);
    } catch (const std::bad_alloc &) {
        return failure(QStringLiteral("PCV_STITCH_001"),
                       QStringLiteral("多帧配准内存不足，正式输出未更新"));
    } catch (const std::exception &exception) {
        return failure(QStringLiteral("PCV_STITCH_001"),
                       QStringLiteral("多帧配准发生异常：%1")
                           .arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        return failure(QStringLiteral("PCV_STITCH_001"),
                       QStringLiteral("多帧配准发生未知异常，正式输出未更新"));
    }
}

} // namespace pcv::interface
