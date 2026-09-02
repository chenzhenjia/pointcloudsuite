#include <pcv/camera/mock_frame_source.h>

#include <pcv/camera/frame_converter.h>

#include <QDateTime>

#include <cmath>

namespace pcv::camera {

MockFrameSource::MockFrameSource(std::uint32_t width, std::uint32_t height)
    : m_width(width)
    , m_height(height)
{
}

CameraError MockFrameSource::start()
{
    m_running.store(true);
    emitFrame();
    return {};
}

void MockFrameSource::stop()
{
    m_running.store(false);
}

bool MockFrameSource::isRunning() const
{
    return m_running.load();
}

QString MockFrameSource::deviceSummary() const
{
    return QStringLiteral("mock point-cloud source %1x%2").arg(m_width).arg(m_height);
}

PointCloudConvertResult MockFrameSource::convertFrame(
    const CameraFrame &frame, const PointCloudConvertOptions &options)
{
    switch (frame.frameType) {
    case FrameType::PointCloudABC32f:
        return convertPointCloudToPoints(frame, options);
    case FrameType::DepthC16:
        return convertDepthToPoints(frame, options);
    case FrameType::ProfileABC16:
        return convertProfileToPoints(frame, options);
    default:
        break;
    }
    PointCloudConvertResult result;
    result.error = QStringLiteral("unsupported frame type for conversion");
    return result;
}

LatestFrameSlot &MockFrameSource::frameSlot()
{
    return m_slot;
}

void MockFrameSource::emitFrame()
{
    m_slot.publish(makeSyntheticFrame(++m_frameCounter));
}

void MockFrameSource::emitException(const QString &description)
{
    auto event = std::make_shared<CameraFrame>();
    event->kind = FrameKind::Exception;
    event->frameType = FrameType::Undefined;
    event->valid = false;
    event->exceptionDescription = description;
    event->exceptionType = 0;
    event->timeStamp = QDateTime::currentMSecsSinceEpoch();
    m_slot.publish(std::move(event));
}

std::shared_ptr<const CameraFrame> MockFrameSource::makeSyntheticFrame(std::uint32_t frameNumber) const
{
    auto frame = std::make_shared<CameraFrame>();
    frame->frameType = FrameType::PointCloudABC32f;
    frame->width = m_width;
    frame->height = m_height;
    frame->frameNumber = frameNumber;
    frame->timeStamp = qint64(frameNumber) * 1000;
    frame->valid = true;

    const qsizetype pointCount = qsizetype(m_width) * m_height;
    QByteArray payload;
    payload.resize(pointCount * 12);
    auto *raw = reinterpret_cast<float *>(payload.data());
    for (qsizetype i = 0; i < pointCount; ++i) {
        const float x = float(i % m_width) * 0.1f;
        const float y = float(i / m_width) * 0.1f;
        // Gentle sine surface so the rendered cloud is not a flat plane.
        const float z = 0.5f * std::sin(x * 0.25f) * std::cos(y * 0.25f);
        raw[i * 3] = x;
        raw[i * 3 + 1] = y;
        raw[i * 3 + 2] = z;
    }
    frame->data = payload;
    return frame;
}

} // namespace pcv::camera
