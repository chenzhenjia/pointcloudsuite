#include <pcv/camera/frame_converter.h>
#include <pcv/camera/frame_data.h>

#include <QCoreApplication>

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {
bool require(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

pcv::camera::CameraFrame makePointCloudFrame(std::initializer_list<float> xyz,
                                             bool valid = true)
{
    pcv::camera::CameraFrame frame;
    frame.frameType = pcv::camera::FrameType::PointCloudABC32f;
    frame.width = std::uint32_t(xyz.size() / 3);
    frame.height = 1;
    frame.valid = valid;
    QByteArray payload;
    payload.resize(int(xyz.size()) * 4);
    std::memcpy(payload.data(), xyz.begin(), payload.size());
    frame.data = payload;
    return frame;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Basic ABC32f -> Point3D conversion.
    {
        auto frame = makePointCloudFrame({1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
        const auto result = pcv::camera::convertPointCloudToPoints(frame);
        if (!require(result.ok && result.points.size() == 2,
                     "ABC32f frame should convert to two points")) return 1;
        if (!require(result.points[0].x == 1.0f && result.points[0].y == 2.0f
                         && result.points[0].z == 3.0f,
                     "first point coordinates should match")) return 1;
        if (!require(result.points[1].x == 4.0f && result.points[1].z == 6.0f,
                     "second point coordinates should match")) return 1;
        if (!require(result.totalSamples == 2 && result.skipped == 0,
                     "totalSamples should be 2 with no skipped points")) return 1;
    }

    // Non-finite points are skipped by default.
    {
        auto frame = makePointCloudFrame({1.0f, 2.0f, 3.0f,
                                          std::nanf(""), 5.0f, 6.0f,
                                          7.0f, 8.0f, std::numeric_limits<float>::infinity()});
        const auto result = pcv::camera::convertPointCloudToPoints(frame);
        if (!require(result.ok && result.points.size() == 1 && result.skipped == 2,
                     "non-finite points should be skipped by default")) return 1;
        if (!require(result.points[0].x == 1.0f, "surviving point should be the finite one")) return 1;
    }

    // skipZero option removes exactly-zero triples.
    {
        auto frame = makePointCloudFrame({0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
        pcv::camera::PointCloudConvertOptions options;
        options.skipZero = true;
        const auto result = pcv::camera::convertPointCloudToPoints(frame, options);
        if (!require(result.ok && result.points.size() == 1 && result.skipped == 1,
                     "skipZero should drop the zero triple")) return 1;
    }

    // Invalid frame (bValid == false) is rejected.
    {
        auto frame = makePointCloudFrame({1.0f, 2.0f, 3.0f}, false);
        const auto result = pcv::camera::convertPointCloudToPoints(frame);
        if (!require(!result.ok && result.points.isEmpty(),
                     "invalid frame must be rejected")) return 1;
    }

    // Non-point-cloud frame type is rejected.
    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::DepthC16;
        frame.valid = true;
        frame.data = QByteArray(8, '\0');
        const auto result = pcv::camera::convertPointCloudToPoints(frame);
        if (!require(!result.ok, "depth frame must be rejected by the point-cloud converter")) return 1;
    }

    // Malformed payload length is rejected.
    {
        auto frame = makePointCloudFrame({1.0f, 2.0f, 3.0f, 4.0f});
        frame.data = frame.data.left(10); // not a multiple of 12
        const auto result = pcv::camera::convertPointCloudToPoints(frame);
        if (!require(!result.ok, "payload length not a multiple of 12 must be rejected")) return 1;
    }

    // ABC32f must also match the advertised raster dimensions.
    {
        auto frame = makePointCloudFrame({1.0f, 2.0f, 3.0f});
        frame.width = 2;
        if (!require(!pcv::camera::convertPointCloudToPoints(frame).ok,
                     "point-cloud payload must match width x height")) return 1;
    }


    // Depth C16 -> Point3D using scale/offset.
    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::DepthC16;
        frame.width = 2;
        frame.height = 2;
        frame.valid = true;
        frame.xScale = 0.5f;
        frame.yScale = 1.0f;
        frame.zScale = 0.1f;
        frame.xOffset = 0;
        frame.yOffset = 0;
        frame.zOffset = -5;
        QByteArray payload;
        payload.resize(4 * 2);
        auto *raw = reinterpret_cast<qint16 *>(payload.data());
        raw[0] = 50;  // (0,0) -> x=0 y=0 z=0
        raw[1] = 60;  // (1,0) -> x=0.5 y=0 z=1
        raw[2] = 70;  // (0,1) -> x=0 y=1 z=2
        raw[3] = 80;  // (1,1) -> x=0.5 y=1 z=3
        frame.data = payload;
        const auto result = pcv::camera::convertDepthToPoints(frame);
        if (!require(result.ok && result.points.size() == 4,
                     "depth C16 frame should convert to four points")) return 1;
        if (!require(result.points[0].x == 0.0f && result.points[0].y == 0.0f
                         && result.points[0].z == 0.0f,
                     "depth point (0,0) coordinate should match")) return 1;
        if (!require(result.points[1].x == 0.5f && result.points[1].y == 0.0f
                         && result.points[1].z == 1.0f,
                     "depth point (1,0) coordinate should match")) return 1;
        if (!require(result.points[3].x == 0.5f && result.points[3].y == 1.0f
                         && result.points[3].z == 3.0f,
                     "depth point (1,1) coordinate should match")) return 1;
    }

    // Profile ABC16 -> Point3D.
    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::ProfileABC16;
        frame.width = 1;
        frame.height = 2;
        frame.valid = true;
        frame.xScale = 0.25f;
        frame.yScale = 0.5f;
        frame.zScale = 1.0f;
        QByteArray payload;
        payload.resize(2 * 6);
        auto *raw = reinterpret_cast<qint16 *>(payload.data());
        raw[0] = 4; raw[1] = 2; raw[2] = 10;   // -> (1, 1, 10)
        raw[3] = -4; raw[4] = 0; raw[5] = -20; // -> (-1, 0, -20)
        frame.data = payload;
        const auto result = pcv::camera::convertProfileToPoints(frame);
        if (!require(result.ok && result.points.size() == 2,
                     "profile ABC16 frame should convert to two points")) return 1;
        if (!require(result.points[0].x == 1.0f && result.points[0].y == 1.0f
                         && result.points[0].z == 10.0f,
                     "profile point 0 should be scaled correctly")) return 1;
        if (!require(result.points[1].x == -1.0f && result.points[1].z == -20.0f,
                     "profile point 1 should be scaled correctly")) return 1;
    }

    // Raster-shaped payloads fail closed when their dimensions do not match
    // the byte payload. A DP2240 depth image must not use a fabricated layout.
    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::DepthC16;
        frame.width = 2;
        frame.height = 2;
        frame.valid = true;
        frame.data = QByteArray(2 * 3, '\0');
        const auto result = pcv::camera::convertDepthToPoints(frame);
        if (!require(!result.ok && result.error.contains(QStringLiteral("width x height")),
                     "depth payload must match width x height")) return 1;
    }

    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::DepthC16;
        frame.valid = true;
        frame.data = QByteArray(2, '\0');
        const auto result = pcv::camera::convertDepthToPoints(frame);
        if (!require(!result.ok && result.error.contains(QStringLiteral("dimensions")),
                     "depth frame with zero dimensions must be rejected")) return 1;
    }

    // Depth frame is rejected by the point-cloud converter and vice versa.
    {
        pcv::camera::CameraFrame frame;
        frame.frameType = pcv::camera::FrameType::DepthC16;
        frame.valid = true;
        frame.data = QByteArray(8, '\0');
        if (!require(!pcv::camera::convertPointCloudToPoints(frame).ok,
                     "depth frame must be rejected by the point-cloud converter")) return 1;
        auto pc = makePointCloudFrame({1.0f, 2.0f, 3.0f});
        if (!require(!pcv::camera::convertDepthToPoints(pc).ok,
                     "point-cloud frame must be rejected by the depth converter")) return 1;
    }

    std::cout << "frame_converter_tests: PASS\n";
    return 0;
}
