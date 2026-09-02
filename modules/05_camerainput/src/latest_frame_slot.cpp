#include <pcv/camera/frame_source.h>

namespace pcv::camera {

bool LatestFrameSlot::publish(std::shared_ptr<const CameraFrame> frame)
{
    std::function<void()> notify;
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dropped = static_cast<bool>(m_latest);
        m_latest = std::move(frame);
        notify = m_onFramePublished;
    }
    // Notify outside the lock so the consumer can take() immediately.
    if (notify) notify();
    return dropped;
}

void LatestFrameSlot::setFramePublishedCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onFramePublished = std::move(callback);
}

void LatestFrameSlot::clearFramePublishedCallback()
{
    setFramePublishedCallback({});
}

std::shared_ptr<const CameraFrame> LatestFrameSlot::take()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_latest, nullptr);
}

std::uint32_t LatestFrameSlot::latestFrameNumber() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest ? m_latest->frameNumber : 0u;
}

} // namespace pcv::camera
