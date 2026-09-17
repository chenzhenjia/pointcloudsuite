#include <pcv/interface/stitching_interface.h>

#include <pcv/io/cloud_cache.h>
#include <pcv/io/artifact_transaction.h>

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThreadPool>
#include <QTextStream>
#include <QtConcurrent>

#include <atomic>
#include <cmath>
#include <exception>
#include <memory>
#include <new>
#include <limits>

namespace pcv::interface {
namespace {

QVector<pointcloud::Point3D> previewPoints(const QVector<pointcloud::Point3D> &points,
                                           double ratio);
QByteArray mappingBytes(const pointcloud::MultiFrameRegistrationResult &merge);

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

QVector<pointcloud::Point3D> previewPoints(const QVector<pointcloud::Point3D> &points, double ratio)
{
    ratio = qBound(0.01, ratio, 1.0);
    if (ratio >= 0.999) return points;
    const quint64 threshold = quint64(ratio * static_cast<long double>(std::numeric_limits<quint64>::max()));
    QVector<pointcloud::Point3D> preview;
    for (const auto &point : points) {
        const qint64 x = qint64(std::floor(double(point.x) / 0.2));
        const qint64 y = qint64(std::floor(double(point.y) / 0.2));
        const qint64 z = qint64(std::floor(double(point.z) / 0.2));
        quint64 value = quint64(x) * 73856093ULL ^ quint64(y) * 19349663ULL ^ quint64(z) * 83492791ULL;
        value ^= value >> 30; value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27; value *= 0x94D049BB133111EBULL; value ^= value >> 31;
        if (value <= threshold) preview.push_back(point);
    }
    return preview;
}

QByteArray mappingBytes(const pointcloud::MultiFrameRegistrationResult &merge)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0x314D4350) << quint32(1) << quint64(merge.points.size());
    for (qsizetype i = 0; i < merge.points.size(); ++i)
        stream << qint32(merge.cloudIds.value(i, -1))
               << qint64(merge.sourceIndices.value(i, -1))
               << merge.scanRatios.value(i, 0.0f);
    return stream.status() == QDataStream::Ok ? bytes : QByteArray{};
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
    if (options.frames.size() < (options.registrationOptions.enabled ? 2 : 1))
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

    pointcloud::IcpOptions registrationOptions = options.useRegistrationOptions
        ? options.registrationOptions : pointcloud::IcpOptions{};
    registrationOptions.sampleStride = options.sampleStride;
    registrationOptions.collectDiagnosticFrames = options.collectDiagnosticFrames;
    registrationOptions.structuralCoverageOfMatched = options.structuralCoverageOfMatched;
    registrationOptions.minimumStructuralMatchedRatio = options.minimumStructuralMatchedRatio;
    registrationOptions.visibilityWarningRatio = options.visibilityWarningRatio;
    registrationOptions.tangentCoarseTrimmedScore = options.tangentCoarseTrimmedScore;
    registrationOptions.correspondenceParallelTasks = options.correspondenceParallelTasks;
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
    result.merge = registration;
    qInfo() << "Stitching ICP completed, ok=" << registration.ok
            << "points=" << registration.points.size();
    if (!registration.ok) {
        result.cancelled = registration.cancelled;
        result.errorCode = registration.cancelled ? QString() : QStringLiteral("PCV_STITCH_001");
        result.message = registration.error;
        return result;
    }
    for (int index = 1; index < registration.icpDiagnostics.size(); ++index) {
        const pointcloud::IcpDiagnostics &diagnostic = registration.icpDiagnostics[index];
        if (!diagnostic.accepted) {
            result.errorCode = QStringLiteral("PCV_STITCH_001");
            // The failure code is already part of the reason text; the visibility
            // numbers are added here because they decide whether the pair is
            // simply not comparable (little common surface) or misregistered.
            result.message = QStringLiteral("第 %1 帧配准未通过（%2）：%3；命中率=%4，命中点内双向 3 mm=%5")
                .arg(index + 1)
                .arg(diagnostic.failureCode.isEmpty() ? QStringLiteral("未分类") : diagnostic.failureCode)
                .arg(diagnostic.reason)
                .arg(QString::number(double(diagnostic.structuralBidirectionalMatchRatio), 'f', 4))
                .arg(QString::number(double(diagnostic.structuralBidirectionalCoverage3mmOfMatched), 'f', 4));
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
        result.seamFusion = seam;
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
        result.merge = registration;
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
    } else {
        result.seamFusion.ok = true;
        result.seamFusion.inputPoints = registration.points.size();
        result.seamFusion.outputPoints = registration.points.size();
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
    pcv::io::ArtifactTransaction transaction(options.outputDirectory,
                                              options.allowOverwrite,
                                              cancelled);
    const QVector<pointcloud::Point3D> preview = previewPoints(registration.points, options.previewRatio);
    const QString previewName = QStringLiteral("stitched_robot_base_preview.ply");
    const QString mappingName = QStringLiteral("stitched_robot_base.pcvmap");
    const QString reportName = QStringLiteral("stitching_manifest.json");
    QJsonObject manifestJson;
    manifestJson.insert(QStringLiteral("schema"), QStringLiteral("pcv-stitching-manifest-v0.4"));
    manifestJson.insert(QStringLiteral("coordinate_frame"), QStringLiteral("robot_base"));
    manifestJson.insert(QStringLiteral("formal_ply"), QStringLiteral("stitched_robot_base.ply"));
    manifestJson.insert(QStringLiteral("preview_ply"), previewName);
    manifestJson.insert(QStringLiteral("mapping_file"), mappingName);
    manifestJson.insert(QStringLiteral("success"), true);
    manifestJson.insert(QStringLiteral("diagnostics"), result.diagnostics);
    bool outputWritten = transaction.stageBinaryPly(
        QStringLiteral("stitched_robot_base.ply"), registration.points, false,
        {QStringLiteral("coordinate_frame robot_base")})
        && transaction.stageBinaryPly(previewName, preview, false,
                                      {QStringLiteral("coordinate_frame robot_base")})
        && transaction.stageBytes(mappingName, mappingBytes(registration))
        && transaction.stageJson(reportName,
                                 QJsonDocument(manifestJson).toJson(QJsonDocument::Indented))
        ;
    if (outputWritten && options.writeDiagnosticFrames) {
        for (int index = 0; index < registration.diagnosticPreIcpFrames.size(); ++index) {
            const QString pre = QStringLiteral("diagnostic_pre_icp_frame_%1.ply").arg(index + 1, 2, 10, QChar('0'));
            const QString post = QStringLiteral("diagnostic_post_icp_frame_%1.ply").arg(index + 1, 2, 10, QChar('0'));
            outputWritten = transaction.stageBinaryPly(pre, registration.diagnosticPreIcpFrames.at(index), false)
                && transaction.stageBinaryPly(post, registration.diagnosticPostIcpFrames.value(index), false);
            if (!outputWritten) break;
            result.manifest.diagnosticPreIcpFiles.push_back(QDir(options.outputDirectory).filePath(pre));
            result.manifest.diagnosticPostIcpFiles.push_back(QDir(options.outputDirectory).filePath(post));
        }
    }
    if (outputWritten && options.writeSourceClouds) {
        QVector<QVector<pointcloud::Point3D>> buckets(sourcePaths.size());
        for (qsizetype index = 0; index < registration.points.size(); ++index) {
            const int cloudId = registration.cloudIds.value(index, -1);
            if (cloudId < 0 || cloudId >= buckets.size()) continue;
            buckets[cloudId].push_back(registration.points.at(index));
        }
        for (int cloudId = 0; cloudId < buckets.size(); ++cloudId) {
            const QString name = QStringLiteral("%1_%2_robot_base.ply")
                .arg(cloudId + 1).arg(QFileInfo(sourcePaths.at(cloudId)).completeBaseName());
            outputWritten = transaction.stageBinaryPly(name, buckets.at(cloudId), false);
            if (!outputWritten) break;
            result.manifest.sourcePlyFiles.push_back(QDir(options.outputDirectory).filePath(name));
        }
    }
    if (outputWritten) outputWritten = transaction.commit();
    result.outputMs = outputClock.elapsed();
    if (!outputWritten) {
        detail = transaction.error();
        result.cancelled = detail == QStringLiteral("已取消");
        result.errorCode = result.cancelled ? QString() : QStringLiteral("PCV_OUTPUT_002");
        result.message = detail;
        return result;
    }
    result.success = true;
    result.outputPly = outputPath;
    result.manifest.formalPly = outputPath;
    result.manifest.previewPly = QDir(options.outputDirectory).filePath(previewName);
    result.manifest.mappingFile = QDir(options.outputDirectory).filePath(mappingName);
    result.manifest.reportJson = QDir(options.outputDirectory).filePath(reportName);
    result.points = std::move(registration.points);
    result.merge.points = result.points;
    result.merge.ok = true;
    result.merge.error.clear();
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

StitchingResult stitch(const StitchingRequest &request)
{
    StitchingOptions options;
    options.calibrationPath = request.input.calibrationPath;
    options.frames = request.input.frames;
    options.outputDirectory = request.artifacts.outputDirectory;
    options.allowOverwrite = request.artifacts.allowOverwrite;
    options.registrationOptions = request.registration.options;
    options.useRegistrationOptions = true;
    options.previewRatio = request.artifacts.previewRatio;
    options.writeSourceClouds = request.artifacts.writeSourceClouds;
    options.writeDiagnosticFrames = request.artifacts.writeDiagnosticFrames;
    options.sampleStride = request.registration.sampleStride;
    options.parallelEnabled = request.registration.parallelEnabled;
    options.maxParallelTasks = request.registration.maxParallelTasks;
    options.collectDiagnosticFrames = request.registration.collectDiagnosticFrames;
    options.structuralCoverageOfMatched = request.registration.structuralCoverageOfMatched;
    options.minimumStructuralMatchedRatio = request.registration.minimumStructuralMatchedRatio;
    options.visibilityWarningRatio = request.registration.visibilityWarningRatio;
    options.tangentCoarseTrimmedScore = request.registration.tangentCoarseTrimmedScore;
    options.correspondenceParallelTasks = request.registration.correspondenceParallelTasks;
    options.seamEnabled = request.seam.enabled;
    options.seamHalfWidthMm = request.seam.halfWidthMm;
    options.isCancelled = request.isCancelled;
    options.progress = request.progress;
    return stitchRawLineProfiles(options);
}

} // namespace pcv::interface
