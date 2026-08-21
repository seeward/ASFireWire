#pragma once

#include "../Controller/ControllerTypes.hpp"

namespace ASFW::Driver {

/// Shared status memory is updated for every watchdog/interrupt sample, but
/// waking the app for those samples turns a 1 kHz watchdog into a 1 kHz Mach
/// message stream. Notifications are invalidations, not a telemetry transport:
/// only lifecycle/topology changes need to wake clients.
[[nodiscard]] constexpr bool ShouldNotifyStatusListener(SharedStatusReason reason) noexcept {
    switch (reason) {
    case SharedStatusReason::Boot:
    case SharedStatusReason::BusReset:
    case SharedStatusReason::Manual:
    case SharedStatusReason::Disconnect:
        return true;
    case SharedStatusReason::Interrupt:
    case SharedStatusReason::AsyncActivity:
    case SharedStatusReason::Watchdog:
        return false;
    }
    return false;
}

} // namespace ASFW::Driver
