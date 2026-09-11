#pragma once

#include <pcv/camera/frame_data.h>

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace pcv::camera {

struct IntensityImageResult {
    QByteArray mono8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameNumber = 0;
    qint64 timeStamp = 0;
    bool completeScan = false;
    bool ok = false;
    QString error;
};

// Extracts the owned Mono8 intensity raster from a complete camera frame.
// The intensity payload must contain exactly one byte per raster sample.
IntensityImageResult extractIntensityImage(const CameraFrame &frame);

} // namespace pcv::camera
