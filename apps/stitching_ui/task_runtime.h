#pragma once

#include <pcv/registration/handeye_transform.h>
#include <QRegularExpression>
#include <atomic>
#include <cmath>
#include <memory>

namespace pcv::ui {

// GUI adapters retain their accepted separators; numeric validation is shared.
inline bool parseRobotPose(const QString &text, pointcloud::RobotPose *pose,
                           const QRegularExpression &separators)
{
    if (!pose) return false;
    const auto parts = text.split(separators, Qt::SkipEmptyParts);
    if (parts.size() != 6) return false;
    double values[6]{};
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        values[i] = parts.at(i).toDouble(&ok);
        if (!ok || !std::isfinite(values[i])) return false;
    }
    *pose = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return true;
}

inline bool acceptsTask(quint64 requestedRevision, quint64 currentRevision,
                        const std::shared_ptr<std::atomic_bool> &requestedToken,
                        const std::shared_ptr<std::atomic_bool> &currentToken)
{
    return requestedRevision == currentRevision && requestedToken
        && requestedToken == currentToken && !requestedToken->load();
}

} // namespace pcv::ui
