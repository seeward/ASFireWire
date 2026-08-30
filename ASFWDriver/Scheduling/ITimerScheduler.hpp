#pragma once

// ITimerScheduler — injected, one-shot timer abstraction for protocol control
// planes. Protocol objects are ordinary C++ state, so they cannot own a
// DriverKit OSAction directly and must instead use the driver-owned timer.
//
// Production callbacks run on the driver's Default queue. Host tests provide a
// deterministic virtual-clock implementation. ScheduleAfter() must always
// defer its callback, including when delayNs is zero.

#include <cstdint>
#include <functional>

namespace ASFW::Scheduling {

using TimerToken = uint64_t;
inline constexpr TimerToken kInvalidTimerToken = 0;

class ITimerScheduler {
public:
    virtual ~ITimerScheduler() = default;

    [[nodiscard]] virtual TimerToken ScheduleAfter(uint64_t delayNs,
                                                    std::function<void()> fn) = 0;

    // Removes a callback that has not begun execution. A callback already
    // dispatched by the scheduler may complete, so callers must also use their
    // own epoch or lifetime guard before making externally visible changes.
    virtual void Cancel(TimerToken token) = 0;
};

} // namespace ASFW::Scheduling
