#include <pcv/interface/stitching_interface.h>

#include <pcv/io/cloud_cache.h>

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <cmath>
#include <exception>
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
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法创建输出 PLY：%1").arg(path);
        return false;
    }
    QTextStream header(&file);
    header << "ply\nformat binary_little_endian 1.0\n"
           << "comment coordinate_frame robot_base\n"
           << "element vertex " << points.size() << "\n"
           << "property float x\nproperty float y\nproperty float z\nend_header\n";
    header.flush();
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    data.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for (qsizetype index = 0; index < points.size(); ++index) {
        if ((index & 0x3fff) == 0 && isCancelled && isCancelled()) {
            file.cancelWriting();
            if (error) *error = QStringLiteral("已取消");
            return false;
        }
        const pointcloud::Point3D &point = points[index];
        data << point.x << point.y << point.z;
    }
    if (data.status() != QDataStream::Ok || !file.commit()) {
        if (error) *error = QStringLiteral("无法提交输出 PLY：%1").arg(path);
        return false;
    }
    return true;
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
    if (options.frames.size() < 2)
        return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("配准至少需要两个 PLY 文件"));
    if (options.sampleStride < 1 || options.sampleStride > 100)
        return failure(QStringLiteral("PCV_CONTRACT_001"), QStringLiteral("配准抽样步长必须在 1 到 100 之间"));
    if (options.seamEnabled)
        return failure(QStringLiteral("PCV_STITCH_001"), QStringLiteral("当前版本暂时禁用渐变接缝融合"));
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
    QVector<pointcloud::RobotBaseFrame> frames;
    frames.reserve(options.frames.size());
    for (int index = 0; index < options.frames.size(); ++index) {
        if (cancelled()) {
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

        progress(0.15f * float(index) / float(options.frames.size()),
                 QStringLiteral("读取并转换点云 %1/%2：%3").arg(index + 1).arg(options.frames.size())
                     .arg(QFileInfo(sourcePath).fileName()));
        pcv::detail::io::PlyReadOptions readOptions;
        readOptions.isCancelled = cancelled;
        const auto cloud = pcv::detail::io::readPlyCached(sourcePath, {}, readOptions);
        if (!cloud.ok) {
            StitchingResult result = failure(cloud.cancelled ? QString() : QStringLiteral("PCV_INPUT_002"), cloud.error);
            result.cancelled = cloud.cancelled;
            return result;
        }
        pointcloud::CloudTransformOptions transformOptions;
        transformOptions.layout = pointcloud::DepthPointLayout::LineProfileXz;
        transformOptions.sampleStride = options.sampleStride;
        transformOptions.isCancelled = cancelled;
        auto transformed = pointcloud::transformLineScanToRobotBase(
            cloud.points, calibration, input.startPose, input.endPose, transformOptions);
        if (!transformed.ok) {
            StitchingResult result = failure(transformed.cancelled ? QString() : QStringLiteral("PCV_TRANSFORM_001"), transformed.error);
            result.cancelled = transformed.cancelled;
            return result;
        }
        pointcloud::RobotBaseFrame frame;
        frame.sourceFile = sourcePath;
        frame.fullPoints = std::move(transformed.points);
        frame.samplePoints = std::move(transformed.samplePoints);
        frame.sourceIndices = std::move(transformed.sourceIndices);
        frame.scanRatios = std::move(transformed.scanRatios);
        frame.startBaseFromFlange = pointcloud::robotPoseToMatrix(input.startPose);
        frame.endBaseFromFlange = pointcloud::robotPoseToMatrix(input.endPose);
        frame.declaredCount = cloud.points.size();
        frame.rejectedInvalid = transformed.rejectedInvalid;
        frame.rejectedRange = transformed.rejectedRange;
        frame.inputYMinimum = transformed.inputYMinimum;
        frame.inputYMaximum = transformed.inputYMaximum;
        frame.signedTravel = transformed.signedTravel;
        frame.dominantTravelAxis = transformed.dominantTravelAxis;
        frames.push_back(std::move(frame));
    }

    qInfo() << "Stitching hand-eye conversion completed, frames=" << frames.size();

    pointcloud::IcpOptions registrationOptions;
    registrationOptions.sampleStride = options.sampleStride;
    registrationOptions.isCancelled = cancelled;
    auto registration = pointcloud::registerRobotBaseFrames(std::move(frames), registrationOptions,
        [&progress](float value, const QString &message) { progress(0.15f + value * 0.70f, message); });
    StitchingResult result;
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
        seamOptions.isCancelled = cancelled;
        const auto seam = pointcloud::applyTrajectorySeamFusion(&registration, seamOptions);
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
            if (!diagnostic.applied) {
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
    if (!writeRobotBasePly(outputPath, registration.points, cancelled, &detail)) {
        result.cancelled = detail == QStringLiteral("已取消");
        result.errorCode = result.cancelled ? QString() : QStringLiteral("PCV_OUTPUT_002");
        result.message = detail;
        return result;
    }
    result.success = true;
    result.outputPly = outputPath;
    result.points = std::move(registration.points);
    result.diagnostics += QStringLiteral("formal_output=%1\n").arg(outputPath);
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
