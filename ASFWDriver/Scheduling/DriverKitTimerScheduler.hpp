#pragma once

#include "ITimerScheduler.hpp"

#ifdef ASFW_HOST_TEST
#include "../Testing/HostDriverKitStubs.hpp"
#else
#include <DriverKit/IODispatchQueue.h>
#include <DriverKit/IOTimerDispatchSource.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/OSAction.h>
#include <DriverKit/OSSharedPtr.h>
#endif

#include <functional>
#include <map>
#include <vector>

class ASFWDriver;

namespace ASFW::Scheduling {

// Production implementation of ITimerScheduler. A single DriverKit timer
// source wakes the next due control-plane callback; callbacks then run on the
// driver's Default queue.
class DriverKitTimerScheduler final : public ITimerScheduler {
public:
    DriverKitTimerScheduler();
    ~DriverKitTimerScheduler() override;

    DriverKitTimerScheduler(const DriverKitTimerScheduler&) = delete;
    DriverKitTimerScheduler& operator=(const DriverKitTimerScheduler&) = delete;

    [[nodiscard]] kern_return_t Prepare(::ASFWDriver& service,
                                        OSSharedPtr<IODispatchQueue> workQueue);
    void Reset() noexcept;

    [[nodiscard]] TimerToken ScheduleAfter(uint64_t delayNs,
                                           std::function<void()> fn) override;
    void Cancel(TimerToken token) override;

    void HandleTimerFired() noexcept;

private:
    struct PendingCallback {
        uint64_t deadlineTicks{0};
        std::function<void()> fn;
    };

    // Earliest pending deadline in mach-absolute ticks (0 = none). Caller holds lock_.
    [[nodiscard]] uint64_t EarliestDeadlineLocked() const noexcept;
    // Arms the hardware timer. MUST be called WITHOUT lock_ held: WakeAtTime is
    // RPC-dispatched to the timer's queue (ASFWDriver-Default), whose handlers
    // re-enter the scheduler and take lock_. Holding lock_ across it is an AB-BA
    // deadlock (kernel registry busy-timeout panic, 2026-06-22).
    void ArmTimerUnlocked(IOTimerDispatchSource* timer, uint64_t deadlineTicks) noexcept;
    [[nodiscard]] uint64_t DeadlineTicksFromNow(uint64_t delayNs) const noexcept;

    IOLock* lock_{nullptr};
    OSSharedPtr<IODispatchQueue> workQueue_{};
    OSSharedPtr<IOTimerDispatchSource> timer_{};
    OSSharedPtr<OSAction> action_{};
    std::map<TimerToken, PendingCallback> pending_;
    TimerToken nextToken_{1};
};

} // namespace ASFW::Scheduling
