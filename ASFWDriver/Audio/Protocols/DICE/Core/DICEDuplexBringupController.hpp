// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// DICEDuplexBringupController.hpp - Generic async duplex startup state machine for DICE devices

#pragma once

#include "../../Duplex/IDuplexDeviceControl.hpp"
#include "DICERestartSession.hpp"
#include "DICETypes.hpp"
#include "DICETransaction.hpp"
#include "../../../../Protocols/Ports/ProtocolRegisterIO.hpp"
#include "../../../../Protocols/Ports/FireWireBusPort.hpp"
#include "../../../../Scheduling/ITimerScheduler.hpp"
#include <DriverKit/IODispatchQueue.h>
#include <DriverKit/IOReturn.h>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <variant>

namespace ASFW::Audio::DICE {

// DICE devices normally require a stable GLOBAL source-lock indication before
// stream enable and confirmation. Some playback-only products need host IT
// packets before their selected receive-clock path can report that lock; those
// products retain the target-rate check but treat source lock as post-start
// telemetry instead of an admission gate.
struct DICEBringupPolicy final {
    bool requireSourceLockBeforeStreamEnable{true};
    bool requireSourceLockAtConfirm{true};
};

/// Manages generic DICE duplex startup/teardown.
///
/// All long-running methods (PrepareDuplex48k, ProgramRxForDuplex48k,
/// ProgramTxAndEnableDuplex48k, ConfirmDuplex48kStart, ReleaseOwner) are fully async. Delayed
/// retry continuations use the injected timer scheduler rather than blocking a DriverKit queue.
/// StopDuplex remains a transitional synchronous compatibility boundary.
class DICEDuplexBringupController {
public:
    using VoidCallback = std::function<void(IOReturn)>;
    using PrepareCallback = IDuplexDeviceControl::PrepareCallback;
    using StageCallback = IDuplexDeviceControl::StageCallback;
    using ConfirmCallback = IDuplexDeviceControl::ConfirmCallback;
    using ClockApplyCallback = IDuplexDeviceControl::ClockApplyCallback;
    using SectionLayoutCallback = std::function<void(IOReturn, GeneralSections)>;

    DICEDuplexBringupController(
        DICETransaction& diceReader,
        Protocols::Ports::ProtocolRegisterIO& io,
        Protocols::Ports::FireWireBusInfo& busInfo,
        IODispatchQueue* workQueue,
        GeneralSections sections,
        Scheduling::ITimerScheduler* timerScheduler = nullptr,
        DICEBringupPolicy bringupPolicy = {});

    ~DICEDuplexBringupController();

    DICEDuplexBringupController(const DICEDuplexBringupController&) = delete;
    DICEDuplexBringupController& operator=(const DICEDuplexBringupController&) = delete;

    // Async duplex methods (were IOReturn, now void + callback)
    void PrepareDuplex(const AudioDuplexChannels& channels,
                       const DiceClockConfiguration& desiredClock,
                       PrepareCallback callback);
    void ProgramRx(StageCallback callback);
    void ProgramTxAndEnableDuplex(StageCallback callback);
    void ConfirmDuplexStart(ConfirmCallback callback);
    void ApplyClockConfig(const DiceClockConfiguration& desiredClock,
                          ClockApplyCallback callback);
    void RefineClockForStreamGeometry(const DiceClockConfiguration& currentClock,
                                      ClockApplyCallback callback);
    /// A product stopped-prepare hook may replace the active DICE stream
    /// image. Refresh its general section pointers before ProgramRx/ProgramTx
    /// so their register writes address the replacement image.
    void RefreshPreparedSectionLayout(SectionLayoutCallback callback);
    void SetTeardownCancelToken(const std::atomic<bool>* cancel) noexcept {
        teardownCancel_ = cancel;
    }

    // Transitional wrappers for existing non-coordinator callers.
    void PrepareDuplex48k(const AudioDuplexChannels& channels, VoidCallback callback);
    void ProgramRxForDuplex48k(VoidCallback callback);
    void ProgramTxAndEnableDuplex48k(VoidCallback callback);
    void ConfirmDuplex48kStart(VoidCallback callback);
    [[nodiscard]] IOReturn StopDuplex();       // stays sync — pure writes, no HW wait
    void ReleaseOwner(VoidCallback callback);

    [[nodiscard]] bool IsPrepared() const noexcept;
    [[nodiscard]] bool IsArmed() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool IsOwnerClaimed() const noexcept;

private:
    enum class FlowMode : uint8_t {
        kNone,
        kPrepareDuplex,
        kClockApply,
    };

    // The former controller state was a restart-session bag with a phase enum
    // and several independently mutable booleans.  That admitted impossible
    // combinations such as "idle but owner claimed" and (critically) allowed a
    // forced CLOCK_SELECT write to complete from an unrelated locked status.
    //
    // Keep the generic DuplexRestartPhase at the public result boundary, but
    // model the DICE device workflow internally as a closed set of states.  A
    // state which can wait for CLOCK_ACCEPTED is distinct from ordinary
    // preparation, so a status poll can never bypass that required transition.
    struct Operation final {
        FW::Generation generation{0};
        AudioDuplexChannels channels{};
        DiceRestartReason reason{DiceRestartReason::kInitialStart};
        AudioClockConfig desiredClock{};
        AudioClockConfig appliedClock{};
        FlowMode flow{FlowMode::kNone};
        bool ownerClaimed{false};
        bool forceClockSelectWrite{false};
        uint32_t preClaimClockSelect{0};
        bool preClaimDeviceEnabled{false};
    };

    struct IdleState final {};
    struct PreparingState final { Operation operation{}; };
    struct AwaitingClockAcceptedState final { Operation operation{}; };
    struct PreparedState final { Operation operation{}; };
    struct ProgrammingRxState final { Operation operation{}; };
    struct RxProgrammedState final { Operation operation{}; };
    struct ProgrammingTxState final { Operation operation{}; };
    struct TxArmedState final { Operation operation{}; };
    struct ConfirmingState final { Operation operation{}; };
    struct RunningState final { Operation operation{}; };
    struct StoppingState final { Operation operation{}; };
    struct FailedState final { Operation operation{}; IOReturn error{kIOReturnSuccess}; };

    using State = std::variant<IdleState,
                               PreparingState,
                               AwaitingClockAcceptedState,
                               PreparedState,
                               ProgrammingRxState,
                               RxProgrammedState,
                               ProgrammingTxState,
                               TxArmedState,
                               ConfirmingState,
                               RunningState,
                               StoppingState,
                               FailedState>;

    [[nodiscard]] Operation* ActiveOperation() noexcept;
    [[nodiscard]] const Operation* ActiveOperation() const noexcept;
    [[nodiscard]] bool HasActiveOperation() const noexcept;
    [[nodiscard]] DiceRestartPhase CurrentPhase() const noexcept;
    void ResetOperation() noexcept;

    template <typename NextState>
    void TransitionTo() noexcept {
        static_assert(!std::same_as<NextState, IdleState>);
        Operation* operation = ActiveOperation();
        if (operation == nullptr) {
            return;
        }
        Operation next = std::move(*operation);
        state_ = NextState{.operation = std::move(next)};
    }

    void BeginOperation(FW::Generation generation,
                        const AudioDuplexChannels& channels,
                        DiceRestartReason reason,
                        const DiceClockConfiguration& clock,
                        FlowMode flow,
                        bool forceClockSelectWrite) noexcept;

    // Async step chain for PrepareDuplex48k raw-parity path
    void BeginClockApply(const DiceClockConfiguration& desiredClock,
                         bool forceClockSelectWrite,
                         ClockApplyCallback callback);
    void DoReadGlobalStatus(AudioDuplexChannels channels, VoidCallback cb);
    void DoRefreshSectionLayout(AudioDuplexChannels channels, VoidCallback cb);
    void DoReadGlobalBeforeClaim(AudioDuplexChannels channels, VoidCallback cb);
    void DoReadOwnerBeforeClaim(AudioDuplexChannels channels, VoidCallback cb);
    void DoClaimOwner(AudioDuplexChannels channels, VoidCallback cb);
    void DoReadOwnerAfterClaim(AudioDuplexChannels channels, VoidCallback cb);
    void DoNormalizeGlobalEnable(AudioDuplexChannels channels, VoidCallback cb);
    void DoWriteClockSelect(AudioDuplexChannels channels, VoidCallback cb);
    void DoActiveClockCheck(AudioDuplexChannels channels, uint32_t accumulatedNotify, VoidCallback cb);
    void DoWaitClockAccepted(AudioDuplexChannels channels, uint32_t attempt, VoidCallback cb);
    void DoConfirmClockAccepted(AudioDuplexChannels channels, uint32_t observedNotify, VoidCallback cb);
    void DoReadGlobalAfterClockAccepted(AudioDuplexChannels channels, uint32_t observedNotify, IOReturn failureStatus, VoidCallback cb);
    // Gate between clock-confirm and stream enable: always wait for the target
    // rate. The product policy decides whether GLOBAL source lock is also
    // required before the host stream can provide its clock reference.
    void DoAwaitStreamingClockLock(AudioDuplexChannels channels, uint32_t attempt, VoidCallback cb);
    void DoDiscoverStreams(AudioDuplexChannels channels, uint32_t step, VoidCallback cb);
    // Per-stream stop-side disables. The stop sequence must clear EVERY stream's
    // ISOC register (FFADO stopStreamByIndex writes 0xFFFFFFFF per stream); a
    // stream[0]-only clear leaves the secondary's stale channel in the device,
    // which the next bring-up's FirstActiveIsoChannel then adopts as stream[0]
    // (channel-map drift) — and a start over a stale duplicate channel wedges the
    // device until a power cycle.
    void DoStopDisableTxStream(uint32_t streamIndex, uint32_t entrySizeBytes, bool releaseOwner, VoidCallback cb);
    void DoStopDisableRxStream(uint32_t streamIndex, uint32_t entrySizeBytes, bool releaseOwner, VoidCallback cb);
    // Per-stream device programming. A multi-stream DICE device (e.g. Venice F32,
    // 2×16 channels) requires every advertised stream's ISOC register written
    // before the single GLOBAL_ENABLE, so these recurse over the stream index.
    // entrySizeBytes is the per-stream register stride (kSize*4), read once at
    // streamIndex 0 and threaded through.
    void DoProgramRx(AudioDuplexChannels channels, uint32_t streamIndex,
                     uint32_t entrySizeBytes, VoidCallback cb);
    void DoProgramTx(AudioDuplexChannels channels, uint32_t streamIndex,
                     uint32_t entrySizeBytes, VoidCallback cb);
    void DoEnableGlobal(AudioDuplexChannels channels, VoidCallback cb);
    void DoFinishPrepare(VoidCallback cb);
    void DoRollback(IOReturn error, VoidCallback cb);
    void DoCompleteClockApply(VoidCallback cb);
    void RefreshRuntimeCaps(VoidCallback cb);

    // Async step chain for ConfirmDuplex48kStart
    void DoPollSourceLock(uint32_t attempt, uint32_t accumulatedNotify, VoidCallback cb);
    void DoCompleteConfirm(uint32_t notification, uint32_t status, uint32_t extStatus,
                           VoidCallback cb);

    // Async stop / rollback sequencing
    void DoStopSequence(bool releaseOwner, VoidCallback cb);
    void DoStopDisableGlobal(bool releaseOwner, VoidCallback cb);
    void DoStopDisableTx(bool releaseOwner, VoidCallback cb);
    void DoStopReleaseTx(bool releaseOwner, VoidCallback cb);
    void DoStopDisableRx(bool releaseOwner, VoidCallback cb);
    void DoStopReleaseRx(bool releaseOwner, VoidCallback cb);
    void DoStopReleaseOwner(VoidCallback cb);

    [[nodiscard]] bool EnsureRouteCurrent() const noexcept;
    [[nodiscard]] bool TeardownRequested() const noexcept;
    [[nodiscard]] uint64_t OwnerValue() const noexcept;
    void RecordStopTeardownAbort(const char* stage) const noexcept;
    bool AbortStopIfTeardown(const char* stage, VoidCallback& cb);

    [[nodiscard]] bool ScheduleRetry(uint64_t delayMs, std::function<void()> work);
    void CancelScheduledRetry() noexcept;

    DICETransaction& diceReader_;
    Protocols::Ports::ProtocolRegisterIO& io_;
    Protocols::Ports::FireWireBusInfo& busInfo_;
    IODispatchQueue* workQueue_;   // NOT owned — borrowed from caller
    Scheduling::ITimerScheduler* timerScheduler_{nullptr};  // NOT owned — driver lifetime
    GeneralSections sections_;
    DICEBringupPolicy bringupPolicy_{};

    // One delayed continuation is valid for this serialized state machine at a time. The epoch
    // makes an already-dispatched timer harmless after a new operation, rollback, or teardown.
    std::atomic<Scheduling::TimerToken> scheduledRetry_{Scheduling::kInvalidTimerToken};
    std::atomic<uint64_t> scheduledRetryEpoch_{0};

    State state_{IdleState{}};
    DiceClockConfiguration diceClock_{};
    AudioStreamRuntimeCaps runtimeCaps_{};
    uint32_t confirmNotification_{0};
    uint32_t confirmStatus_{0};
    uint32_t confirmExtStatus_{0};
    IOReturn stopSequenceError_{kIOReturnSuccess};
    bool refreshRuntimeCapsOnPrepare_{true};
    // Retains the reason for an equal-value clock reselect. Every DICE clock
    // operation writes CLOCK_SELECT; the flag only annotates cold-geometry
    // refinement in diagnostics.
    const std::atomic<bool>* teardownCancel_{nullptr};
};

} // namespace ASFW::Audio::DICE
