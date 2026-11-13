#pragma once
#include <pcv/camera/capture_session_v1.h>

namespace pcv::camera::detail {
bool saveDepthFrame(const QString &root, quint64 id, const CameraFrame &frame,
                    bool intensity, CaptureFrameRecord *record, QString *error,
                    const QString &calibrationPath = {}, bool pureRaw = false,
                    const QString &deviceModel = {}, const QString &deviceSerial = {});
CaptureFrameData readDepthFrame(const QString &directory,
                               const std::function<bool()> &cancelled, CaptureFrameReadMode mode);
}
