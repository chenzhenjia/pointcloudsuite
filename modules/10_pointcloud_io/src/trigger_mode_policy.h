#pragma once

#include <cstdint>

namespace pcv::camera::detail {

// A few DP-series firmware versions do not expose the optional GenICam-style
// trigger nodes.  Those errors are non-fatal only for continuous acquisition;
// software-trigger mode must fail closed instead of claiming support.
inline bool isOptionalTriggerNodeStatus(std::uint32_t status)
{
    return status == 0x80060001u /* MV3D_LP_E_SUPPORT */
        || status == 0x80060004u /* MV3D_LP_E_PARAMETER */;
}

inline bool continuousTriggerProbeMayContinue(std::uint32_t status)
{
    return isOptionalTriggerNodeStatus(status);
}

inline bool softwareTriggerProbeMustReject(std::uint32_t status)
{
    return isOptionalTriggerNodeStatus(status);
}

} // namespace pcv::camera::detail
