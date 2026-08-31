#pragma once

#include <QMatrix4x4>
#include <QString>

namespace pcv::stitching {

// Pure input data for a world-space stitching frame. Processing and UI state
// remain outside this DTO so the orchestration module stays reusable.
struct WorldCloudInput {
    QString filePath;
    QMatrix4x4 startBaseFromFlange;
    QMatrix4x4 endBaseFromFlange;
    QMatrix4x4 flangeFromDepth;
};

} // namespace pcv::stitching
