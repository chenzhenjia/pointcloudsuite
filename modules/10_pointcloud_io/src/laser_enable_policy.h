#pragma once

#include <QString>

#include <optional>

namespace pcv::camera::detail {

// Vendor parameter that switches the camera built-in laser. The Mv3dLp SDK
// accepts it only before MV3D_LP_StartMeasure, so the wrapper probes, writes
// and reads it back once between OpenDeviceBySN() and StartMeasure(), and
// never touches it from the SDK callback thread.
inline const char *const kLaserEnableKey = "LaserEnable";

// Operator default: the camera built-in laser is enabled. Turning it off
// selects external-laser mode, which is driven by an external controller (or
// by hand) and is never controlled by this application.
inline constexpr bool kDefaultLaserEnabled = true;

// A LaserEnable request only takes effect at the next start(). While streaming
// the device must not be reconfigured, so a late request is rejected instead
// of being applied at an unspecified moment.
inline bool laserRequestApplicable(bool streaming)
{
    return !streaming;
}

// The device counts as configured only when the value read back after the
// write equals the requested value. An unsupported parameter, a failed write,
// or any other readback fails start() closed.
inline bool laserEnableReadbackMatches(bool requested, bool readback)
{
    return requested == readback;
}

inline QString laserEnableValueText(bool value)
{
    return value ? QStringLiteral("1 (enabled)") : QStringLiteral("0 (disabled)");
}

inline QString laserEnableProbeError(const QString &statusText)
{
    return QStringLiteral("MV3D_LP_GetParam(%1) failed: %2")
        .arg(QString::fromLatin1(kLaserEnableKey))
        .arg(statusText);
}

inline QString laserEnableSetError(bool requested, const QString &statusText)
{
    return QStringLiteral("MV3D_LP_SetParam(%1=%2) failed: %3")
        .arg(QString::fromLatin1(kLaserEnableKey))
        .arg(laserEnableValueText(requested))
        .arg(statusText);
}

inline QString laserEnableReadbackError(bool requested,
                                        std::optional<bool> readback,
                                        const QString &statusText)
{
    if (!readback.has_value()) {
        return QStringLiteral("MV3D_LP_GetParam(%1) failed after requesting %2: %3")
            .arg(QString::fromLatin1(kLaserEnableKey))
            .arg(laserEnableValueText(requested))
            .arg(statusText);
    }
    return QStringLiteral("%1 readback mismatch: requested %2, effective %3 (%4)")
        .arg(QString::fromLatin1(kLaserEnableKey))
        .arg(laserEnableValueText(requested))
        .arg(laserEnableValueText(*readback))
        .arg(statusText);
}

} // namespace pcv::camera::detail
