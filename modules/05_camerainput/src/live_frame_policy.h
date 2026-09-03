#pragma once

#include <pcv/camera/frame_data.h>

#include <QString>

#include <optional>

namespace pcv::camera::detail {

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
