#include "depth_capture_storage_v1.h"
#include <pcv/camera/frame_converter.h>
#include <pcv/io/artifact_transaction.h>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <cstring>

namespace pcv::camera::detail {
namespace {
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
template<class T> QByteArray encode(const QVector<T> &values) {
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
    for (T value : values) stream << value;
    return bytes;
}
bool readChecked(const QDir &dir, const QString &name, qint64 size, const QString &sha,
                 const std::function<bool()> &cancelled, QByteArray *out, QString *error) {
    QFile file(dir.filePath(name));
    if (size < 0 || size > 256 * 1024 * 1024 || !file.open(QIODevice::ReadOnly) || file.size() != size) {
        *error = QStringLiteral("深度帧文件缺失或长度错误：%1").arg(name); return false;
    }
    out->reserve(qsizetype(size));
    while (!file.atEnd()) {
        if (cancelled && cancelled()) { *error = QStringLiteral("cancelled"); return false; }
        const auto block = file.read(1024 * 1024);
        if (block.isEmpty()) { *error = file.errorString(); return false; }
        out->append(block);
    }
    if (digest(*out) != sha) { *error = QStringLiteral("深度帧校验失败：%1").arg(name); return false; }
    return true;
}
}

bool saveDepthFrame(const QString &root, quint64 id, const CameraFrame &frame,
                    bool intensity, CaptureFrameRecord *record, QString *error) {
    QElapsedTimer timer; timer.start();
    const quint64 samples = quint64(frame.width) * frame.height;
    if (frame.frameType != FrameType::DepthC16 || !frame.valid || !frame.completeScan
        || !samples || samples > (256 * 1024 * 1024 / 12) || frame.data.size() != qsizetype(samples * 2)
        || frame.sdkVersion.isEmpty() || frame.conversionFingerprint.isEmpty()
        || (!frame.intensityData.isEmpty() && frame.intensityData.size() != qsizetype(samples))
        || (!frame.exposureTimeStamps.isEmpty() && frame.exposureTimeStamps.size() != frame.height)
        || (!frame.encoders.isEmpty() && frame.encoders.size() != frame.height)) {
        *error = QStringLiteral("原始深度帧及转换元数据无效，拒绝保存"); return false;
    }
    const auto exposure = encode(frame.exposureTimeStamps), encoder = encode(frame.encoders);
    QJsonObject meta{{"schema", "pcv-camera-frame-v2"}, {"id", qint64(id)},
        {"width", qint64(frame.width)}, {"height", qint64(frame.height)},
        {"sdk_frame_number", qint64(frame.frameNumber)}, {"timestamp", frame.timeStamp},
        {"coordinate_frame", "camera"}, {"units", "mm"}, {"progress_source", "raster_row"},
        {"boundary_source", frame.boundarySource == ScanBoundarySource::SdkImageFrame ? "sdk_image" : "fixed_rows"},
        {"storage", "depth_c16"}, {"byte_order", "little_endian"}, {"depth_bytes", frame.data.size()},
        {"depth_sha256", digest(frame.data)}, {"sdk_version", frame.sdkVersion},
        {"conversion_fingerprint", frame.conversionFingerprint},
        {"scale", QJsonArray{frame.xScale, frame.yScale, frame.zScale}},
        {"offset", QJsonArray{frame.xOffset, frame.yOffset, frame.zOffset}},
        {"sdk_reserved", QString::fromLatin1(frame.sdkReserved.toHex())},
        {"exposure_bytes", exposure.size()}, {"exposure_sha256", digest(exposure)},
        {"encoder_bytes", encoder.size()}, {"encoder_sha256", digest(encoder)},
        {"conversion_intensity_bytes", frame.intensityData.size()},
        {"conversion_intensity_sha256", digest(frame.intensityData)}};
    const QString folder = QStringLiteral("frame_%1").arg(id, 8, 10, QLatin1Char('0'));
    pcv::io::ArtifactTransaction tx(QDir(root).filePath(folder));
    if (!tx.stageBytes("depth.c16", frame.data)
        || (!exposure.isEmpty() && !tx.stageBytes("exposure.i64", exposure))
        || (!encoder.isEmpty() && !tx.stageBytes("encoder.i32", encoder))
        || (!frame.intensityData.isEmpty() && !tx.stageBytes("conversion_intensity.mono8",frame.intensityData))) { *error = tx.error(); return false; }
    qint64 savedBytes = frame.data.size() + exposure.size() + encoder.size() + frame.intensityData.size();
    if (intensity) {
        if (frame.intensityWidth != frame.width || frame.intensityHeight != frame.height
            || frame.intensityData.size() != qsizetype(samples)) {
            *error = QStringLiteral("当前扫描缺少有效 Mono8 亮度图"); return false;
        }
        QImage image(reinterpret_cast<const uchar *>(frame.intensityData.constData()), int(frame.width),
                     int(frame.height), int(frame.width), QImage::Format_Grayscale8);
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) { *error = QStringLiteral("无法编码亮度 PNG"); return false; }
        meta.insert("intensity_format", "Mono8"); meta.insert("intensity_valid", true);
        meta.insert("intensity_width", qint64(frame.width)); meta.insert("intensity_height", qint64(frame.height));
        meta.insert("intensity_bytes", frame.intensityData.size());
        meta.insert("intensity_sha256", digest(frame.intensityData));
        if (!tx.stageBytes("intensity.mono8", frame.intensityData) || !tx.stageBytes("intensity.png", png)) {
            *error = tx.error(); return false;
        }
        savedBytes += frame.intensityData.size() + png.size();
    }
    const auto json = QJsonDocument(meta).toJson();
    if (!tx.stageJson("frame.json", json) || !tx.commit()) { *error = tx.error(); return false; }
    *record = {QDir(root).filePath(folder), id, frame.width, frame.height, frame.frameNumber, frame.timeStamp, 0};
    record->depthAvailable = true; record->intensityAvailable = intensity;
    record->writeWorkNs = timer.nsecsElapsed(); record->savedBytes = savedBytes + json.size();
    return true;
}

CaptureFrameData readDepthFrame(const QString &directory, const std::function<bool()> &cancelled,
                               CaptureFrameReadMode mode) {
    CaptureFrameData result; const QDir dir(directory);
    QFile json(dir.filePath("frame.json"));
    if (!json.open(QIODevice::ReadOnly) || json.size() > 1024 * 1024) { result.error = QStringLiteral("无法读取深度帧元数据"); return result; }
    const auto meta = QJsonDocument::fromJson(json.readAll()).object();
    const qint64 w = meta.value("width").toInteger(), h = meta.value("height").toInteger();
    if (meta.value("schema") != "pcv-camera-frame-v2" || meta.value("storage") != "depth_c16"
        || meta.value("byte_order") != "little_endian" || meta.value("coordinate_frame") != "camera"
        || meta.value("units") != "mm" || meta.value("progress_source") != "raster_row"
        || w < 1 || w > UINT32_MAX || h < 2 || h > UINT32_MAX
        || quint64(w) * quint64(h) > (256 * 1024 * 1024 / 12)
        || meta.value("depth_bytes").toInteger() != w * h * 2 || meta.value("id").toInteger() < 1) {
        result.error = QStringLiteral("深度帧元数据无效"); return result;
    }
    auto frame = std::make_shared<CameraFrame>();
    frame->frameType = frame->rawFrameType = FrameType::DepthC16;
    frame->width = quint32(w); frame->height = quint32(h); frame->valid = true;
    frame->boundarySource = meta.value("boundary_source") == "sdk_image" ? ScanBoundarySource::SdkImageFrame : ScanBoundarySource::FixedRows;
    frame->frameNumber = quint32(meta.value("sdk_frame_number").toInteger()); frame->timeStamp = meta.value("timestamp").toInteger();
    frame->sdkVersion = meta.value("sdk_version").toString(); frame->conversionFingerprint = meta.value("conversion_fingerprint").toString();
    frame->sdkReserved = QByteArray::fromHex(meta.value("sdk_reserved").toString().toLatin1());
    const auto scales = meta.value("scale").toArray(), offsets = meta.value("offset").toArray();
    if (scales.size() != 3 || offsets.size() != 3 || frame->sdkVersion.isEmpty() || frame->conversionFingerprint.size() != 64
        || frame->sdkReserved.size() != 8) { result.error = QStringLiteral("深度转换元数据缺失"); return result; }
    for (int i = 0; i < 3; ++i) {
        if (!scales.at(i).isDouble() || !std::isfinite(scales.at(i).toDouble()) || !offsets.at(i).isDouble()
            || offsets.at(i).toDouble() < INT32_MIN || offsets.at(i).toDouble() > INT32_MAX) {
            result.error = QStringLiteral("深度转换参数无效"); return result;
        }
    }
    frame->xScale = float(scales.at(0).toDouble()); frame->yScale = float(scales.at(1).toDouble()); frame->zScale = float(scales.at(2).toDouble());
    frame->xOffset = qint32(offsets.at(0).toInteger()); frame->yOffset = qint32(offsets.at(1).toInteger()); frame->zOffset = qint32(offsets.at(2).toInteger());
    if (!readChecked(dir, "depth.c16", w*h*2, meta.value("depth_sha256").toString(), cancelled, &frame->data, &result.error)) return result;
    for (const QString &kind : {QStringLiteral("exposure"), QStringLiteral("encoder")}) {
        const qint64 bytes = meta.value(kind + "_bytes").toInteger(-1);
        const bool exposure = kind == "exposure";
        if (bytes != 0 && bytes != h * (exposure ? 8 : 4)) { result.error = QStringLiteral("逐行元数据长度错误"); return result; }
        if (bytes) {
            QByteArray data;
            if (!readChecked(dir, exposure ? "exposure.i64" : "encoder.i32", bytes, meta.value(kind+"_sha256").toString(), cancelled, &data, &result.error)) return result;
            QDataStream stream(data); stream.setByteOrder(QDataStream::LittleEndian);
            for (qint64 i = 0; i < h; ++i) {
                if (exposure) { qint64 value; stream >> value; frame->exposureTimeStamps.push_back(value); }
                else { qint32 value; stream >> value; frame->encoders.push_back(value); }
            }
        }
    }
    const qint64 conversionIntensityBytes = meta.value("conversion_intensity_bytes").toInteger(-1);
    if (conversionIntensityBytes != 0 && conversionIntensityBytes != w*h) {
        result.error = QStringLiteral("转换辅助亮度载荷长度错误"); return result;
    }
    if (conversionIntensityBytes) {
        if (!readChecked(dir,"conversion_intensity.mono8",w*h,meta.value("conversion_intensity_sha256").toString(),cancelled,
                         &frame->intensityData,&result.error)) return result;
        frame->intensityWidth = quint32(w); frame->intensityHeight = quint32(h);
    }
    const bool withIntensity = mode == CaptureFrameReadMode::WithIntensity || mode == CaptureFrameReadMode::PreviewOnly;
    if (withIntensity && meta.value("intensity_format") == "Mono8") {
        if (!readChecked(dir, "intensity.mono8", w*h, meta.value("intensity_sha256").toString(), cancelled, &result.intensityMono8, &result.error)) return result;
        QImage image(dir.filePath("intensity.png"));
        if (image.size() != QSize(int(w), int(h))) { result.error = QStringLiteral("亮度 PNG 缺失或尺寸错误"); return result; }
        image = image.convertToFormat(QImage::Format_Grayscale8);
        for (int row = 0; row < h; ++row)
            if (std::memcmp(image.constScanLine(row), result.intensityMono8.constData() + row*w, size_t(w))) {
                result.error = QStringLiteral("亮度 PNG 与 Mono8 不一致"); return result;
            }
        result.intensityWidth = quint32(w); result.intensityHeight = quint32(h); result.intensityPngPath = dir.filePath("intensity.png");
        if (frame->intensityData != result.intensityMono8) { result.error = QStringLiteral("亮度图与 SDK 转换辅助数据不一致"); return result; }
    }
    result.rawFrame = frame;
    result.record = {directory, quint64(meta.value("id").toInteger()), quint32(w), quint32(h), frame->frameNumber, frame->timeStamp, 0};
    result.record.depthAvailable = true; result.record.intensityAvailable = !result.intensityMono8.isEmpty();
    if (mode == CaptureFrameReadMode::WithIntensity || mode == CaptureFrameReadMode::GeometryOnly) {
        if (cancelled && cancelled()) { result.error = QStringLiteral("cancelled"); return result; }
        auto converted = materializeCameraFrame(*frame);
        if (!converted.ok) { result.error = converted.error; return result; }
        result.points = std::move(converted.points); result.sourceIndices = std::move(converted.sourceIndices);
        result.record.pointCount = result.points.size();
    }
    result.ok = !(cancelled && cancelled());
    if (!result.ok) result.error = QStringLiteral("cancelled");
    return result;
}
}
