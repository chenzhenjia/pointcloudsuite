#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace pcv::camera {

// Image mode selector understood by the Hikvision 3D laser profile sensor.
// Only point-cloud mode is supported by this module; other modes are listed
// for validation and diagnostics.
enum class ImageMode : std::uint32_t {
    OriginImage = 1,
    PointCloudImage = 4,
    RangeImage = 7
};

// Frame payload type, aligned with the SDK's Mv3dLpImageType values.
enum class FrameType : std::uint32_t {
    Undefined = 0xFFFFFFFFu,
    Mono8 = 0x01080001u,
    DepthC16 = 0x011000B8u,
    ProfileABC16 = 0x023000B9u,
    PointCloudABC32f = 0x026000C0u,
    Rgb24Packed = 0x02180014u,
    Jpeg = 0x80180001u,
    ProfileABC32 = 0x82603001u
};

// Distinguishes data frames from device/connection events that travel the
// same frame slot (e.g. disconnection reported by the exception callback).
enum class FrameKind : std::uint8_t {
    Frame = 0,       // A point-cloud / depth / profile data frame.
    Exception = 1    // Device event (offline etc.), not a data frame.
};

// Owned copy of one camera frame. The callback thread copies the raw bytes
// out of the SDK buffer into this struct before handing it to the latest
// frame slot, so the SDK buffer lifetime never escapes the callback.
struct CameraFrame {
    FrameKind kind = FrameKind::Frame;
    FrameType frameType = FrameType::Undefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Raw payload bytes (ABC32f: 12 bytes per point; C16: 2 bytes per pixel).
    QByteArray data;
    // Optional intensity channel (Mono8), same pixel count as the depth frame.
    QByteArray intensityData;
    std::uint32_t frameNumber = 0;
    qint64 timeStamp = 0;
    bool valid = false;
    // Filled for FrameKind::Exception events with the vendor description.
    QString exceptionDescription;
    std::uint32_t exceptionType = 0;

    // Per-axis sampling scale/offset. In point-cloud mode the coordinates are
    // already metric and these fields are carried for diagnostics only.
    float xScale = 1.0f;
    float yScale = 1.0f;
    float zScale = 1.0f;
    std::int32_t xOffset = 0;
    std::int32_t yOffset = 0;
    std::int32_t zOffset = 0;

    // Scan assembly diagnostics. A real DP-series device can deliver one
    // complete scan in several profile batches; only complete scans are
    // published to consumers.
    std::uint32_t expectedScanRows = 0;
    std::uint32_t accumulatedScanRows = 0;
    std::uint32_t scanFragmentCount = 0;
    std::uint32_t discardedScanCount = 0;
    bool completeScan = true;

    // Expected point count for the raw data block.
    qsizetype dataPointCount() const
    {
        if (frameType == FrameType::PointCloudABC32f) {
            return data.size() / 12;
        }
        if (frameType == FrameType::DepthC16) {
            return data.size() / 2;
        }
        return 0;
    }
};

// Small immutable description of a discovered device.
struct DeviceInfo {
    QString modelName;
    QString serialNumber;
    QString currentIp;
    QString userDefinedName;
    // True for emulated devices (3DMVS virtual camera / nTLayerType 16).
    bool isVirtual = false;
};

// Carries a human-readable reason for diagnostics.
struct CameraError {
    QString message;
    bool ok() const { return message.isEmpty(); }
};

} // namespace pcv::camera
