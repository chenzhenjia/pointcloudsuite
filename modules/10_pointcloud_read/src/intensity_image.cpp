#include <pcv/camera/intensity_image.h>

#include <limits>

namespace pcv::camera {

IntensityImageResult extractIntensityImage(const CameraFrame &frame)
{
    IntensityImageResult result;
    result.width = frame.width;
    result.height = frame.height;
    result.frameNumber = frame.frameNumber;
    result.timeStamp = frame.timeStamp;
    result.completeScan = frame.completeScan;

    if (frame.kind != FrameKind::Frame) {
        result.error = QStringLiteral("camera event is not an image frame");
        return result;
    }
    if (!frame.valid) {
        result.error = frame.exceptionDescription.isEmpty()
            ? QStringLiteral("frame is marked invalid")
            : frame.exceptionDescription;
        return result;
    }
    if (!frame.completeScan) {
        result.error = QStringLiteral("frame is not a complete scan");
        return result;
    }
    if (frame.width == 0 || frame.height == 0) {
        result.error = QStringLiteral("intensity frame dimensions must be non-zero");
        return result;
    }
    const std::uint64_t sampleCount = std::uint64_t(frame.width) * frame.height;
    if (sampleCount > std::uint64_t(std::numeric_limits<qsizetype>::max())
        || frame.intensityData.size() != qsizetype(sampleCount)) {
        result.error = QStringLiteral("intensity payload length does not match width x height");
        return result;
    }

    result.mono8 = frame.intensityData;
    result.ok = true;
    return result;
}

} // namespace pcv::camera
