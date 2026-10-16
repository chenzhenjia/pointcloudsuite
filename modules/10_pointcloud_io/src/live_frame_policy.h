#pragma once

#include <pcv/camera/frame_data.h>

#include <QString>

#include <optional>

namespace pcv::camera::detail {

inline bool preservesDeviceTrigger(const CaptureConfiguration &configuration)
{
    return configuration.boundaryMode == CaptureBoundaryMode::SdkImageFrame;
}

inline QString sdkImageBoundaryError(const CameraFrame &frame,
                                    const CaptureConfiguration &configuration)
{
    if (!configuration.sdkImageBoundaryVerified)
        return QStringLiteral("请先确认该设备/触发配置下，一次门控扫描对应一幅完整SDK图像");
    if (frame.rawFrameType != FrameType::DepthC16 && frame.rawFrameType != FrameType::PointCloudABC32f)
        return QStringLiteral("未确认的轮廓分片类型不支持自动完整扫描接收");
    constexpr quint64 maximumSamples = (256 * 1024 * 1024) / 12;
    const quint64 samples = quint64(frame.width) * frame.height;
    if (!frame.valid || frame.width == 0 || frame.height < 2 || samples > maximumSamples
        || frame.frameType != FrameType::PointCloudABC32f || quint64(frame.data.size()) != samples * 12
        || (!frame.intensityData.isEmpty() && quint64(frame.intensityData.size()) != samples))
        return QStringLiteral("SDK整幅图像无效、尺寸/载荷不符或超过单帧负载上限");
    return {};
}

enum class CallbackFrameAction {
    CopyPointCloud,
    ConvertProfileToPointCloud,
    ConvertDepthToPointCloud,
    Reject
};

inline CallbackFrameAction callbackFrameAction(FrameType frameType)
{
    switch (frameType) {
    case FrameType::PointCloudABC32f:
        return CallbackFrameAction::CopyPointCloud;
    case FrameType::ProfileABC16:
        return CallbackFrameAction::ConvertProfileToPointCloud;
    case FrameType::DepthC16:
        return CallbackFrameAction::ConvertDepthToPointCloud;
    default:
        return CallbackFrameAction::Reject;
    }
}

inline QString imageModeFrameTypeError(std::optional<ImageMode> requestedMode,
                                       std::optional<ImageMode> activeMode,
                                       FrameType frameType)
{
    const bool expectsPointCloud = requestedMode == ImageMode::PointCloudImage
        || activeMode == ImageMode::PointCloudImage;
    if (!expectsPointCloud || frameType == FrameType::PointCloudABC32f) {
        return {};
    }

    return QStringLiteral(
               "device output type 0x%1 does not match requested PointCloudImage (ImageMode=4)")
        .arg(std::uint32_t(frameType), 8, 16, QLatin1Char('0'));
}

} // namespace pcv::camera::detail
