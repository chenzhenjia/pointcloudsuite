#include <pcv/camera/frame_converter.h>

#include <cmath>

namespace pcv::camera {

namespace {

bool hasExpectedRasterSampleCount(const CameraFrame &frame,
                                  qsizetype bytesPerSample,
                                  QString *error)
{
    if (frame.width == 0 || frame.height == 0) {
        if (error) *error = QStringLiteral("frame dimensions must be non-zero");
        return false;
    }
    const qsizetype expectedSamples = qsizetype(frame.width) * qsizetype(frame.height);
    const qsizetype actualSamples = frame.data.size() / bytesPerSample;
    if (actualSamples != expectedSamples) {
        if (error) {
            *error = QStringLiteral("frame payload sample count does not match width x height");
        }
        return false;
    }
    return true;
}

} // namespace

PointCloudConvertResult convertPointCloudToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options)
{
    PointCloudConvertResult result;
    if (frame.frameType != FrameType::PointCloudABC32f) {
        result.error = QStringLiteral("frame type is not point cloud (ABC32f)");
        return result;
    }
    if (!frame.valid) {
        result.error = QStringLiteral("frame is marked invalid (possible packet loss)");
        return result;
    }
    const qsizetype bytesPerPoint = 12;
    if (frame.data.size() % bytesPerPoint != 0) {
        result.error = QStringLiteral("point-cloud payload length is not a multiple of 12 bytes");
        return result;
    }
    if (frame.data.isEmpty()) {
        result.error = QStringLiteral("point-cloud payload is empty");
        return result;
    }
    result.totalSamples = frame.data.size() / bytesPerPoint;
    result.points.reserve(result.totalSamples);

    const auto *raw = reinterpret_cast<const float *>(frame.data.constData());
    for (qsizetype i = 0; i < result.totalSamples; ++i) {
        const float x = raw[i * 3];
        const float y = raw[i * 3 + 1];
        const float z = raw[i * 3 + 2];
        if (options.skipNonFinite && (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))) {
            ++result.skipped;
            continue;
        }
        if (options.skipZero && x == 0.0f && y == 0.0f && z == 0.0f) {
            ++result.skipped;
            continue;
        }
        result.points.push_back({x, y, z});
    }
    result.ok = true;
    return result;
}

namespace {
// Shared point-emit helper: validates the coordinate and pushes a Point3D.
void emitPoint(QVector<pointcloud::Point3D> &points, qsizetype &skipped,
               float x, float y, float z,
               const PointCloudConvertOptions &options)
{
    if (options.skipNonFinite && (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))) {
        ++skipped;
        return;
    }
    if (options.skipZero && x == 0.0f && y == 0.0f && z == 0.0f) {
        ++skipped;
        return;
    }
    points.push_back({x, y, z});
}
} // namespace

PointCloudConvertResult convertDepthToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options)
{
    PointCloudConvertResult result;
    if (frame.frameType != FrameType::DepthC16) {
        result.error = QStringLiteral("frame type is not depth (C16)");
        return result;
    }
    if (!frame.valid) {
        result.error = QStringLiteral("frame is marked invalid (possible packet loss)");
        return result;
    }
    if (frame.data.size() % 2 != 0) {
        result.error = QStringLiteral("depth payload length is not a multiple of 2 bytes");
        return result;
    }
    if (!hasExpectedRasterSampleCount(frame, 2, &result.error)) return result;
    result.totalSamples = frame.data.size() / 2;
    result.points.reserve(result.totalSamples);

    const auto *raw = reinterpret_cast<const qint16 *>(frame.data.constData());
    const qsizetype width = qMax<qsizetype>(frame.width, 1);
    for (qsizetype i = 0; i < result.totalSamples; ++i) {
        const qsizetype col = i % width;
        const qsizetype row = i / width;
        const float x = float(col) * frame.xScale + float(frame.xOffset);
        const float y = float(row) * frame.yScale + float(frame.yOffset);
        const float z = float(raw[i]) * frame.zScale + float(frame.zOffset);
        emitPoint(result.points, result.skipped, x, y, z, options);
    }
    result.ok = true;
    return result;
}

PointCloudConvertResult convertProfileToPoints(
    const CameraFrame &frame,
    const PointCloudConvertOptions &options)
{
    PointCloudConvertResult result;
    if (frame.frameType != FrameType::ProfileABC16) {
        result.error = QStringLiteral("frame type is not profile (ABC16)");
        return result;
    }
    if (!frame.valid) {
        result.error = QStringLiteral("frame is marked invalid (possible packet loss)");
        return result;
    }
    if (frame.data.size() % 6 != 0) {
        result.error = QStringLiteral("profile payload length is not a multiple of 6 bytes");
        return result;
    }
    if (!hasExpectedRasterSampleCount(frame, 6, &result.error)) return result;
    result.totalSamples = frame.data.size() / 6;
    result.points.reserve(result.totalSamples);

    const auto *raw = reinterpret_cast<const qint16 *>(frame.data.constData());
    for (qsizetype i = 0; i < result.totalSamples; ++i) {
        const float x = float(raw[i * 3]) * frame.xScale + float(frame.xOffset);
        const float y = float(raw[i * 3 + 1]) * frame.yScale + float(frame.yOffset);
        const float z = float(raw[i * 3 + 2]) * frame.zScale + float(frame.zOffset);
        emitPoint(result.points, result.skipped, x, y, z, options);
    }
    result.ok = true;
    return result;
}

} // namespace pcv::camera
