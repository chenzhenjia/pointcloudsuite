#include <pcv/camera/capture_session_v1.h>
#include <pcv/camera/frame_converter.h>
#include <pcv/io/artifact_transaction.h>
#include <pcv/io/ply_reader.h>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QBuffer>
#include <QImage>
#include <cmath>
#include <limits>
#include <QFileInfo>
#include <QElapsedTimer>
#include "live_frame_policy.h"
#include "depth_capture_storage_v1.h"

namespace pcv::camera {
namespace {
bool outsideSourceTree(const QString &path) {
    const QString resolved = QFileInfo(path).canonicalFilePath();
    const QString source = QDir(QString::fromUtf8(PCV_CAPTURE_SOURCE_ROOT)).canonicalPath();
    return !resolved.isEmpty() && resolved.compare(source, Qt::CaseInsensitive) != 0
        && !resolved.startsWith(source + "/", Qt::CaseInsensitive);
}
QJsonObject jsonFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
bool saveFrame(const QString &root, quint64 id, const CameraFrame &frame,
               bool requireIntensity, CaptureFrameRecord *record,
               QVector<pointcloud::Point3D> *preview, QString *error) {
    QElapsedTimer timer; timer.start();
    const auto converted = materializeCameraFrame(frame);
    const qint64 pointConversionNs = timer.nsecsElapsed();
    if (!converted.ok || converted.points.isEmpty()) {
        *error = converted.error.isEmpty() ? QStringLiteral("扫描没有有效点") : converted.error;
        return false;
    }
    const bool hasIntensity = frame.intensityWidth > 0 && frame.intensityHeight > 0
        && frame.intensityData.size() == qsizetype(frame.intensityWidth) * frame.intensityHeight;
    if (requireIntensity && hasIntensity != !frame.intensityData.isEmpty()) {
        *error = QStringLiteral("亮度图 Mono8 尺寸或数据长度无效");
        return false;
    }
    if (requireIntensity && !hasIntensity && frame.intensityData.isEmpty()) {
        *error = QStringLiteral("当前采集帧缺少有效 Mono8 亮度图");
        return false;
    }
    if (requireIntensity && (frame.intensityWidth != frame.width || frame.intensityHeight != frame.height)) {
        *error = QStringLiteral("亮度图尺寸与点云栅格不一致");
        return false;
    }
    QByteArray pngBytes;
    if (requireIntensity) {
        QImage intensityImage(reinterpret_cast<const uchar *>(frame.intensityData.constData()),
                              int(frame.intensityWidth), int(frame.intensityHeight),
                              int(frame.intensityWidth), QImage::Format_Grayscale8);
        QBuffer pngBuffer(&pngBytes);
        if (!pngBuffer.open(QIODevice::WriteOnly) || !intensityImage.copy().save(&pngBuffer, "PNG")) {
            *error = QStringLiteral("无法编码亮度图 PNG");
            return false;
        }
    }
    timer.restart();
    QByteArray indices;
    QDataStream stream(&indices, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x31524943) << quint32(1) << quint64(converted.sourceIndices.size());
    for (qsizetype index : converted.sourceIndices) stream << quint64(index);
    const QString folder = QStringLiteral("frame_%1").arg(id, 8, 10, QLatin1Char('0'));
    QJsonObject meta{{"schema", "pcv-camera-frame-v1"}, {"id", qint64(id)},
        {"width", qint64(frame.width)}, {"height", qint64(frame.height)},
        {"sdk_frame_number", qint64(frame.frameNumber)}, {"timestamp", frame.timeStamp},
        {"total_samples", qint64(converted.totalSamples)}, {"valid_points", qint64(converted.points.size())},
        {"skipped", qint64(converted.skipped)}, {"coordinate_frame", "camera"}, {"units", "mm"},
        {"progress_source", "raster_row"}, {"cloud", "camera.ply"}, {"indices", "raster_indices.bin"},
        {"boundary_source", frame.boundarySource == ScanBoundarySource::SdkImageFrame ? "sdk_image" : "fixed_rows"},
        {"raw_frame_type", qint64(frame.rawFrameType)},
        {"receive_work_ns", frame.receiveWorkNs}, {"sdk_conversion_ns", frame.sdkConversionNs},
        {"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (requireIntensity) {
        meta.insert("intensity", "intensity.png");
        meta.insert("intensity_raw", "intensity.mono8");
        meta.insert("intensity_format", "Mono8");
        meta.insert("intensity_width", qint64(frame.intensityWidth));
        meta.insert("intensity_height", qint64(frame.intensityHeight));
        meta.insert("intensity_bytes", qint64(frame.intensityData.size()));
        meta.insert("intensity_valid", true);
    }
    pcv::io::ArtifactTransaction tx(QDir(root).filePath(folder));
    if (stream.status() != QDataStream::Ok) { *error = QStringLiteral("来源下标编码失败"); return false; }
    if (!tx.stageBinaryPly("camera.ply", converted.points)) { *error = tx.error().isEmpty() ? QStringLiteral("camera.ply 事务写入失败") : tx.error(); return false; }
    if (!tx.stageBytes("raster_indices.bin", indices)) { *error = tx.error().isEmpty() ? QStringLiteral("raster_indices.bin 事务写入失败") : tx.error(); return false; }
    if (requireIntensity && !tx.stageBytes("intensity.png", pngBytes)) { *error = tx.error().isEmpty() ? QStringLiteral("intensity.png 事务写入失败") : tx.error(); return false; }
    if (requireIntensity && !tx.stageBytes("intensity.mono8", frame.intensityData)) { *error = tx.error().isEmpty() ? QStringLiteral("intensity.mono8 事务写入失败") : tx.error(); return false; }
    if (!tx.stageJson("frame.json", QJsonDocument(meta).toJson())) { *error = tx.error().isEmpty() ? QStringLiteral("frame.json 事务写入失败") : tx.error(); return false; }
    if (!tx.commit()) { *error = tx.error().isEmpty() ? QStringLiteral("采集帧事务提交失败") : tx.error(); return false; }
    *record = {QDir(root).filePath(folder), id, frame.width, frame.height,
               frame.frameNumber, frame.timeStamp, converted.points.size()};
    record->intensityAvailable = requireIntensity;
    record->pointConversionNs = pointConversionNs;
    record->writeWorkNs = timer.nsecsElapsed();
    const QDir saved(record->directory);
    record->savedBytes = QFileInfo(saved.filePath("camera.ply")).size()
        + QFileInfo(saved.filePath("raster_indices.bin")).size()
        + (requireIntensity ? QFileInfo(saved.filePath("intensity.png")).size() : 0)
        + (requireIntensity ? QFileInfo(saved.filePath("intensity.mono8")).size() : 0)
        + QFileInfo(saved.filePath("frame.json")).size();
    const qsizetype limit = qMin<qsizetype>(300000, converted.points.size());
    preview->reserve(limit);
    for (qsizetype i = 0; i < limit; ++i) preview->push_back(converted.points.at(i * converted.points.size() / limit));
    return true;
}
}

CaptureFrameQueue::CaptureFrameQueue(quint32 rows, size_t maximumFrames, qsizetype maximumBytes)
    : CaptureFrameQueue(CaptureConfiguration{CaptureBoundaryMode::FixedRows, rows, false}, maximumFrames, maximumBytes) {}
CaptureFrameQueue::CaptureFrameQueue(CaptureConfiguration configuration, size_t maximumFrames, qsizetype maximumBytes)
    : m_configuration(configuration), m_maximumFrames(maximumFrames), m_maximumBytes(maximumBytes) {}
bool CaptureFrameQueue::push(std::shared_ptr<const CameraFrame> frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_accepting) return false;
    QString reason;
    if (!frame || frame->kind != FrameKind::Frame || !frame->valid || !frame->completeScan
        || (frame->frameType != FrameType::PointCloudABC32f && frame->frameType != FrameType::DepthC16) || frame->height < 2 || frame->width == 0
        || quint64(frame->width) * frame->height > quint64(256 * 1024 * 1024 / 12)
        || quint64(frame->width) * frame->height * (frame->frameType == FrameType::DepthC16 ? 2 : 12) != quint64(frame->data.size())
        || (m_configuration.boundaryMode == CaptureBoundaryMode::FixedRows
            && (m_configuration.fixedRows < 2 || frame->height != m_configuration.fixedRows)))
        reason = frame && !frame->exceptionDescription.isEmpty() ? frame->exceptionDescription
            : QStringLiteral("扫描不完整、行数不符或不是标准 ABC32f 点云");
    if (reason.isEmpty() && m_configuration.boundaryMode == CaptureBoundaryMode::SdkImageFrame) {
        reason = detail::sdkImageBoundaryError(*frame, m_configuration);
        if (reason.isEmpty() && frame->boundarySource != ScanBoundarySource::SdkImageFrame)
            reason = QStringLiteral("驱动未确认SDK整幅图像边界，拒绝按默认completeScan接收");
    }
    if (reason.isEmpty() && m_configuration.requireIntensity) {
        const quint64 samples = quint64(frame->width) * frame->height;
        if (frame->intensityWidth != frame->width || frame->intensityHeight != frame->height
            || frame->intensityData.size() != qsizetype(samples))
            reason = QStringLiteral("当前扫描缺少有效 Mono8 亮度图或尺寸与点云不一致");
    }
    const qsizetype bytes = frame ? frame->data.size() + frame->intensityData.size()
        + frame->exposureTimeStamps.size() * 8 + frame->encoders.size() * 4 : 0;
    if (reason.isEmpty() && (m_frames.size() >= m_maximumFrames || bytes > m_maximumBytes - m_bytes))
        reason = QStringLiteral("连续采集队列已满，已停止接收；请降低采集速度");
    if (!reason.isEmpty()) {
        m_error = reason; m_accepting = false; m_ready.notify_all(); return false;
    }
    m_bytes += bytes; ++m_received;
    m_statistics.receivedBytes += bytes;
    if (m_configuration.requireIntensity) ++m_statistics.intensityReceived;
    m_statistics.receiveWorkNs += frame->receiveWorkNs;
    m_statistics.sdkConversionNs += frame->sdkConversionNs;
    m_statistics.lastWidth = frame->width; m_statistics.lastHeight = frame->height;
    m_frames.push_back({std::move(frame), std::chrono::steady_clock::now()});
    m_statistics.peakPending = qMax(m_statistics.peakPending, m_frames.size());
    m_statistics.peakBytes = qMax(m_statistics.peakBytes, m_bytes);
    m_ready.notify_one();
    return true;
}
std::shared_ptr<const CameraFrame> CaptureFrameQueue::pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_ready.wait(lock, [this] { return !m_frames.empty() || !m_accepting; });
    if (m_frames.empty()) return {};
    auto entry = std::move(m_frames.front()); m_frames.pop_front();
    m_statistics.queueWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - entry.enqueued).count();
    auto frame = std::move(entry.frame);
    m_bytes -= frame->data.size() + frame->intensityData.size()
        + frame->exposureTimeStamps.size() * 8 + frame->encoders.size() * 4; return frame;
}
void CaptureFrameQueue::finish() { std::lock_guard<std::mutex> lock(m_mutex); m_accepting = false; m_ready.notify_all(); }
void CaptureFrameQueue::fail(const QString &reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_error.isEmpty()) m_error = reason;
    m_accepting = false; m_ready.notify_all();
}
QString CaptureFrameQueue::error() const { std::lock_guard<std::mutex> lock(m_mutex); return m_error; }
quint64 CaptureFrameQueue::received() const { std::lock_guard<std::mutex> lock(m_mutex); return m_received; }
size_t CaptureFrameQueue::pending() const { std::lock_guard<std::mutex> lock(m_mutex); return m_frames.size(); }
CaptureQueueStatistics CaptureFrameQueue::statistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto result = m_statistics;
    result.received = m_received; result.pending = m_frames.size(); result.pendingBytes = m_bytes;
    return result;
}

QString createCaptureSession(const QString &parent, quint32 rows, const QString &device, QString *error) {
    return createCaptureSession(parent, CaptureConfiguration{CaptureBoundaryMode::FixedRows, rows, false}, device, error);
}
QString createCaptureSession(const QString &parent, const CaptureConfiguration &configuration, const QString &device, QString *error) {
    if (error) error->clear();
    const bool sdk = configuration.boundaryMode == CaptureBoundaryMode::SdkImageFrame;
    if (parent.isEmpty() || (sdk ? !configuration.sdkImageBoundaryVerified : configuration.fixedRows < 2)) {
        *error = QStringLiteral("保存目录/完整扫描行数无效，或尚未确认SDK整幅图像边界"); return {};
    }
    if (!outsideSourceTree(parent)) {
        *error = QStringLiteral("请选择已存在且位于源码树外的采集保存目录"); return {};
    }
    const QString stem = QStringLiteral("capture_v%1_%2").arg(configuration.storeRawDepth ? 3 : sdk ? 2 : 1)
        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz"));
    QDir dir(parent);
    if (!dir.mkpath(".")) { *error = QStringLiteral("无法创建采集目录"); return {}; }
    QString name = stem;
    for (int i = 1; dir.exists(name); ++i) name = stem + "_" + QString::number(i);
    if (!dir.mkdir(name)) { *error = QStringLiteral("无法创建采集会话"); return {}; }
    pcv::io::ArtifactTransaction tx(dir.filePath(name));
    QJsonObject meta{{"schema", configuration.storeRawDepth ? "pcv-camera-session-v3" : sdk ? "pcv-camera-session-v2" : "pcv-camera-session-v1"},
        {"device", device}, {"progress_source", "raster_row"},
        {"intensity_enabled", configuration.requireIntensity},
        {"external_light_enabled", configuration.externalLightEnabled},
        {"external_light_operator_confirmed", configuration.externalLightOperatorConfirmed},
        {"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (sdk) {
        meta.insert("boundary_mode", "sdk_image");
        meta.insert("boundary_verification", "operator_confirmed_device_configuration");
    } else { meta.insert("rows", qint64(configuration.fixedRows)); meta.insert("boundary_mode", "fixed_rows"); }
    if (!tx.stageJson("session.json", QJsonDocument(meta).toJson()) || !tx.commit()) {
        *error = tx.error(); return {};
    }
    return dir.absoluteFilePath(name);
}
QVector<CaptureFrameRecord> readCaptureSession(const QString &directory, QString *error) {
    if (error) error->clear();
    QVector<CaptureFrameRecord> records;
    const auto meta = jsonFile(QDir(directory).filePath("session.json"));
    const bool depth = meta.value("schema") == "pcv-camera-session-v3";
    const bool sdk = meta.value("schema") == "pcv-camera-session-v2"
        || (depth && meta.value("boundary_mode") == "sdk_image");
    if (sdk ? (meta.value("boundary_mode") != "sdk_image"
                  || meta.value("boundary_verification") != "operator_confirmed_device_configuration")
            : ((!depth && meta.value("schema") != "pcv-camera-session-v1") || meta.value("rows").toInteger() < 2)) {
        *error = QStringLiteral("不是有效的 pcv-camera-session-v1/v2/v3 会话"); return records;
    }
    const QDir dir(directory);
    for (const QString &folder : dir.entryList({"frame_*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto frame = jsonFile(dir.filePath(folder + "/frame.json"));
        // A directory without frame.json was never committed and is not a frame.
        if (!QFile::exists(dir.filePath(folder + "/frame.json"))) continue;
        if (frame.value("schema") != (depth ? "pcv-camera-frame-v2" : "pcv-camera-frame-v1")
            || (sdk ? (frame.value("height").toInteger() < 2 || frame.value("boundary_source") != "sdk_image")
                    : frame.value("height").toInteger() != meta.value("rows").toInteger())
            || frame.value("height").toInteger() > UINT32_MAX || frame.value("width").toInteger() > UINT32_MAX
            || frame.value("id").toInteger() <= 0 || frame.value("width").toInteger() <= 0) {
            *error = QStringLiteral("采集帧元数据无效：%1").arg(folder); return {};
        }
        CaptureFrameRecord record{dir.filePath(folder), quint64(frame.value("id").toInteger()),
            quint32(frame.value("width").toInteger()), quint32(frame.value("height").toInteger()),
            quint32(frame.value("sdk_frame_number").toInteger()), frame.value("timestamp").toInteger(),
            qsizetype(frame.value("valid_points").toInteger())};
        record.depthAvailable = depth;
        const QDir frameDirectory(dir.filePath(folder));
        const qint64 imageWidth = frame.value("intensity_width").toInteger();
        const qint64 imageHeight = frame.value("intensity_height").toInteger();
        const qint64 imageBytes = frame.value("intensity_bytes").toInteger();
        QFile raw(frameDirectory.filePath("intensity.mono8"));
        QImage png(frameDirectory.filePath("intensity.png"));
        const bool rawValid = raw.open(QIODevice::ReadOnly) && raw.size() == imageBytes;
        record.intensityAvailable = frame.value("intensity_format").toString() == QLatin1String("Mono8")
            && frame.value("intensity_valid").toBool()
            && imageWidth == record.width && imageHeight == record.height
            && imageBytes == imageWidth * imageHeight
            && rawValid
            && !png.isNull() && png.width() == imageWidth && png.height() == imageHeight
            && QFile::exists(frameDirectory.filePath("intensity.mono8"))
            && QFile::exists(frameDirectory.filePath("intensity.png"));
        records.push_back(record);
    }
    return records;
}
CaptureFrameData readCapturedFrame(const QString &directory, const std::function<bool()> &cancelled,
                                   CaptureFrameReadMode mode) {
    CaptureFrameData result;
    const QDir dir(directory);
    const auto meta = jsonFile(dir.filePath("frame.json"));
    if (meta.value("schema") == "pcv-camera-frame-v2") return detail::readDepthFrame(directory, cancelled, mode);
    const qint64 width = meta.value("width").toInteger(), height = meta.value("height").toInteger();
    const qint64 count = meta.value("valid_points").toInteger();
    if (meta.value("schema") != "pcv-camera-frame-v1" || meta.value("coordinate_frame") != "camera"
        || meta.value("units") != "mm" || meta.value("progress_source") != "raster_row"
        || width <= 0 || width > UINT32_MAX || height < 2 || height > UINT32_MAX
        || count <= 0 || count > (std::numeric_limits<qint64>::max() - 16) / 8
        || quint64(count) > quint64(width) * quint64(height)) {
        result.error = QStringLiteral("采集帧栅格元数据无效"); return result;
    }
    QFile mapping(dir.filePath("raster_indices.bin"));
    if (!mapping.open(QIODevice::ReadOnly) || mapping.size() != 16 + count * 8) {
        result.error = QStringLiteral("采集帧来源映射缺失或长度错误"); return result;
    }
    QDataStream stream(&mapping); stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic, version; quint64 storedCount;
    stream >> magic >> version >> storedCount;
    if (magic != 0x31524943 || version != 1 || storedCount != quint64(count)) {
        result.error = QStringLiteral("采集帧来源映射版本无效"); return result;
    }
    quint64 previous = 0;
    for (qint64 i = 0; i < count; ++i) {
        if ((i & 4095) == 0 && cancelled && cancelled()) { result.error = QStringLiteral("已取消"); return result; }
        quint64 index; stream >> index;
        if (stream.status() != QDataStream::Ok || index >= quint64(width) * quint64(height)
            || (i && index <= previous)) { result.error = QStringLiteral("采集帧来源下标越界或不递增"); return result; }
        result.sourceIndices.push_back(qsizetype(index)); previous = index;
    }
    pcv::detail::io::PlyReadOptions options; options.isCancelled = cancelled;
    const auto cloud = pcv::detail::io::readPly(dir.filePath("camera.ply"), options);
    if (!cloud.ok || cloud.points.size() != count || cloud.declaredPointCount != quint64(count)) {
        result.error = cloud.error.isEmpty() ? QStringLiteral("采集点云与来源映射点数不一致") : cloud.error; return result;
    }
    result.points = cloud.points;
    const qint64 intensityWidth = meta.value("intensity_width").toInteger();
    const qint64 intensityHeight = meta.value("intensity_height").toInteger();
    const qint64 intensityBytes = meta.value("intensity_bytes").toInteger();
    if ((mode == CaptureFrameReadMode::WithIntensity || mode == CaptureFrameReadMode::PreviewOnly)
        && (intensityWidth > 0 || intensityHeight > 0 || intensityBytes > 0)) {
        if (intensityWidth != width || intensityHeight != height
            || intensityBytes != width * height) {
            result.error = QStringLiteral("亮度图元数据与点云栅格不一致"); return result;
        }
        QFile raw(dir.filePath("intensity.mono8"));
        if (!raw.open(QIODevice::ReadOnly) || raw.size() != intensityBytes) {
            result.error = QStringLiteral("采集帧原始亮度图缺失或长度错误"); return result;
        }
        result.intensityMono8 = raw.readAll();
        if (result.intensityMono8.size() != intensityBytes) {
            result.error = QStringLiteral("采集帧原始亮度图读取不完整"); return result;
        }
        QImage png(dir.filePath("intensity.png"));
        if (png.isNull() || png.width() != width || png.height() != height
            || png.format() == QImage::Format_Invalid) {
            result.error = QStringLiteral("采集帧 PNG 亮度图缺失或尺寸错误"); return result;
        }
        result.intensityWidth = quint32(intensityWidth);
        result.intensityHeight = quint32(intensityHeight);
        result.intensityPngPath = dir.filePath("intensity.png");
    }
    result.record = {directory, quint64(meta.value("id").toInteger()), quint32(width), quint32(height),
        quint32(meta.value("sdk_frame_number").toInteger()), meta.value("timestamp").toInteger(), count};
    result.ok = true; return result;
}
bool writeCaptureParameters(const QString &directory, const QJsonObject &parameters, QString *error) {
    if (!outsideSourceTree(directory)) { *error = QStringLiteral("采集会话必须位于源码树外"); return false; }
    QJsonObject saved = parameters;
    const QString calibration = parameters.value("calibration").toString();
    pcv::io::ArtifactTransaction tx(directory, true);
    if (!calibration.isEmpty() && QFileInfo::exists(calibration)) {
        QFile file(calibration);
        if (!file.open(QIODevice::ReadOnly) || !tx.stageBytes("handeye.xml", file.readAll())) {
            *error = file.isOpen() ? tx.error() : file.errorString(); return false;
        }
        saved.insert("calibration", "handeye.xml");
        saved.insert("calibration_source", calibration);
    }
    if (!tx.stageJson("parameters.json", QJsonDocument(saved).toJson()) || !tx.commit()) {
        *error = tx.error(); return false;
    }
    return true;
}
QJsonObject readCaptureParameters(const QString &directory) { return jsonFile(QDir(directory).filePath("parameters.json")); }
CaptureRecorder::CaptureRecorder(QString directory, quint32 rows)
    : CaptureRecorder(std::move(directory), CaptureConfiguration{CaptureBoundaryMode::FixedRows, rows, false}) {}
CaptureRecorder::CaptureRecorder(QString directory, CaptureConfiguration configuration)
    : m_directory(std::move(directory)), m_configuration(configuration),
      m_queue(std::make_shared<CaptureFrameQueue>(configuration)),
      m_thread([this] { run(); }) {}
CaptureRecorder::~CaptureRecorder() { finish(); }
void CaptureRecorder::finish() { m_queue->finish(); if (m_thread.joinable()) m_thread.join(); }
QVector<CaptureFrameRecord> CaptureRecorder::savedFrames() const { std::lock_guard<std::mutex> lock(m_mutex); return m_saved; }
QVector<pointcloud::Point3D> CaptureRecorder::latestPreview() const { std::lock_guard<std::mutex> lock(m_mutex); return m_preview; }
std::shared_ptr<const CameraFrame> CaptureRecorder::latestDepthFrame() const { std::lock_guard<std::mutex> lock(m_mutex); return m_depthPreview; }
QByteArray CaptureRecorder::latestIntensity() const { std::lock_guard<std::mutex> lock(m_mutex); return m_latestIntensity; }
QSize CaptureRecorder::latestIntensitySize() const { std::lock_guard<std::mutex> lock(m_mutex); return m_latestIntensitySize; }
quint32 CaptureRecorder::latestIntensityFrameNumber() const { std::lock_guard<std::mutex> lock(m_mutex); return m_latestIntensityFrameNumber; }
CaptureRecorder::Statistics CaptureRecorder::statistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Statistics result;
    result.saved = m_saved.size();
    for (const auto &frame : m_saved) {
        result.pointConversionNs += frame.pointConversionNs;
        result.writeWorkNs += frame.writeWorkNs;
        result.savedBytes += frame.savedBytes;
    }
    result.intensitySaved = m_configuration.requireIntensity ? result.saved : 0;
    result.elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_started).count();
    return result;
}
void CaptureRecorder::run() {
    try {
        quint64 id = 1;
        while (auto frame = m_queue->pop()) {
            CaptureFrameRecord record; QString error; QVector<pointcloud::Point3D> preview;
            const bool depth = m_configuration.storeRawDepth;
            const bool saved = depth
                ? detail::saveDepthFrame(m_directory, id++, *frame, m_configuration.requireIntensity, &record, &error)
                : saveFrame(m_directory, id++, *frame, m_configuration.requireIntensity, &record, &preview, &error);
            if (!saved) { m_queue->fail(error); break; }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_saved.push_back(record); m_preview = std::move(preview);
            m_depthPreview = depth ? frame : nullptr;
            if (m_configuration.requireIntensity) {
                m_latestIntensity = frame->intensityData;
                m_latestIntensitySize = QSize(int(frame->intensityWidth), int(frame->intensityHeight));
                m_latestIntensityFrameNumber = frame->frameNumber;
            }
        }
    } catch (const std::exception &e) { m_queue->fail(QStringLiteral("采集保存失败：%1").arg(QString::fromLocal8Bit(e.what()))); }
}
} // namespace pcv::camera
