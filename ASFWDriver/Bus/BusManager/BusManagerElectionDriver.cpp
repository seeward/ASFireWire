// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// BusManagerElectionDriver.cpp — see BusManagerElectionDriver.hpp

#include "BusManagerElectionDriver.hpp"
#include "../IRM/LocalIRMResourceController.hpp"
#include "../IRM/IRMCSRConstants.hpp"
#include "../Timing/PostResetTimingCoordinator.hpp"
#include "../Timing/PostResetTiming.hpp"
#include "../BusResetCoordinator.hpp"
#include "../../Logging/Logging.hpp"
#include "../../Controller/ControllerTypes.hpp"
#include "../../Hardware/HardwareInterface.hpp"

namespace ASFW::Bus {

BusManagerElectionDriver::BusManagerElectionDriver(Deps deps, ASFW::Driver::RolePolicy rolePolicy) noexcept
    : deps_(deps), rolePolicy_(rolePolicy) {}

bool BusManagerElectionDriver::ElectionStillAllowed() const noexcept {
    return active_ &&
           rolePolicy_.roleMode == ASFW::FW::RoleMode::FullBusManager &&
           rolePolicy_.fullBMActivityLevel >= ASFW::FW::FullBMActivityLevel::ElectionOnly;
}

void BusManagerElectionDriver::OnTopologyReady(const ASFW::Driver::TopologySnapshot& snap, uint64_t nowNs) noexcept {
    if (!active_) {
        return;
    }

    if (!ElectionStillAllowed()) {
        return;
    }

    if (snap.localNodeId == Driver::kInvalidPhysicalId || snap.irmNodeId == Driver::kInvalidPhysicalId) {
        ASFW_LOG(Controller, "[BM Election] Skipping election: missing local ID or IRM ID");
        return;
    }

    const uint8_t localNodeId = snap.localNodeId;
    const uint8_t irmNodeId = snap.irmNodeId;
    const uint32_t generation = snap.generation;

    localNodeId_ = localNodeId;
    irmNodeId_ = irmNodeId;

    // Start at most one election sequence per generation. A sequence may contain
    // one bounded transient retry scheduled by HandleCompareSwapResult().
    if (attemptedGeneration_ == generation && attemptsThisGeneration_ >= 1) {
        return;
    }

    // Check if we are already contending for this or a newer generation
    if (inFlight_ && inFlightGen_ >= generation) {
        return;
    }

    bool abdicateObserved = false;
    if (deps_.csrResponder) {
        abdicateObserved = deps_.csrResponder->ConsumeAbdicate();
    }

    BmElectionInputs inputs{
        .mode = rolePolicy_.roleMode,
        .activityLevel = rolePolicy_.fullBMActivityLevel,
        .generation = generation,
        .localId = localNodeId,
        .irmId = irmNodeId,
        .wasIncumbent = wasIncumbent_,
        .abdicateObserved = abdicateObserved,
    };

    DecisionAction action = fsm_.Decide(inputs);
    if (action == DecisionAction::DoNotContend) {
        lastAction_ = 0;
        return;
    }

    lastAction_ = (action == DecisionAction::ContendImmediately) ? 1 : 2;

    // Map DecisionAction to Annex H candidate class
    using namespace Timing;
    BMCandidateClass candidateClass = (action == DecisionAction::ContendImmediately) ? BMCandidateClass::Incumbent : BMCandidateClass::NonIncumbent;

    if (!deps_.timing) {
        ASFW_LOG(Controller, "⚠️ [BM Election] Cannot check Annex H gate: timing coordinator missing");
        return;
    }

    const TimingGateResult gate = deps_.timing->CheckBMGate(generation, candidateClass, nowNs);
    if (gate.state == TimingGateState::ExpiredGeneration) {
        ASFW_LOG(Controller, "[BM Election] Aborting: timing gate expired for generation %u", generation);
        return;
    }

    if (gate.allowed) {
        ASFW_LOG(Controller, "[BM Election] Gate OPEN for gen=%u (class=%u); contending now", generation, static_cast<int>(candidateClass));
        inFlight_ = true;
        inFlightGen_ = generation;
        attemptedGeneration_ = generation;
        attemptsThisGeneration_++;
        Contend(generation, localNodeId, irmNodeId, snap.busBase16);
    } else if (gate.state == TimingGateState::Closed) {
        ASFW_LOG(Controller, "[BM Election] Gate CLOSED for gen=%u (class=%u); scheduling for +%llu ms", 
                 generation, static_cast<int>(candidateClass), gate.remainingNs / 1000000ULL);
        
        if (deps_.scheduler) {
            inFlight_ = true;
            inFlightGen_ = generation;
            std::weak_ptr<BusManagerElectionDriver> weakSelf = shared_from_this();
            (void)deps_.scheduler->ScheduleAfter(gate.remainingNs, [weakSelf, generation, localNodeId, irmNodeId, busBase16 = snap.busBase16, candidateClass]() {
                auto self = weakSelf.lock();
                if (!self || !self->active_) return;
                
                // Final check before contending: Role/Activity policy
                if (!self->ElectionStillAllowed()) {
                    ASFW_LOG(Controller, "[BM Election] Deferred contention suppressed: role/activity changed");
                    self->inFlight_ = false;
                    if (self->deps_.timing) self->deps_.timing->RecordRoleSuppression();
                    return;
                }

                // Final check before contending: Generation
                if (self->deps_.asyncController) {
                    const auto state = self->deps_.asyncController->GetBusStateSnapshot();
                    if (state.generation16 != generation) {
                        ASFW_LOG(Controller, "[BM Election] Aborting deferred contention: generation changed from %u to %u",
                                 generation, state.generation16);
                        self->inFlight_ = false;
                        if (self->deps_.timing) self->deps_.timing->RecordStaleTimerFiring();
                        return;
                    }
                }
                
                // Final check before contending: Re-validate timing gate
                if (self->deps_.timing && self->deps_.monotonicNowNs) {
                    const uint64_t now = self->deps_.monotonicNowNs();
                    const auto finalGate = self->deps_.timing->CheckBMGate(generation, candidateClass, now);
                    if (!finalGate.allowed) {
                        ASFW_LOG(Controller, "[BM Election] Deferred contention aborted: gate still closed (state=%d)", static_cast<int>(finalGate.state));
                        self->inFlight_ = false;
                        return;
                    }
                }

                self->attemptedGeneration_ = generation;
                self->attemptsThisGeneration_++;
                self->Contend(generation, localNodeId, irmNodeId, busBase16);
            });
        }
    }
}

void BusManagerElectionDriver::OnBusReset() noexcept {
    const bool localWasBM = fsm_.Owner() == BmOwner::Local;
    if (localWasBM) {
        wasIncumbent_ = true;
    } else {
        wasIncumbent_ = false;
    }
    fsm_.Reset();
    inFlight_ = false;
    inFlightGen_ = 0;
    attemptsThisGeneration_ = 0;
    lastElectionPath_ = 0;
    lastAction_ = 0;
    if (inFlightHandle_) {
        if (deps_.asyncController) {
            deps_.asyncController->Cancel(inFlightHandle_);
        }
        inFlightHandle_ = {};
    }
    if (retryTimer_ != ASFW::Scheduling::kInvalidTimerToken && deps_.scheduler) {
        deps_.scheduler->Cancel(retryTimer_);
    }
    retryTimer_ = ASFW::Scheduling::kInvalidTimerToken;
}

void BusManagerElectionDriver::Stop() noexcept {
    active_ = false;
    inFlight_ = false;
    if (inFlightHandle_) {
        if (deps_.asyncController) {
            deps_.asyncController->Cancel(inFlightHandle_);
        }
        inFlightHandle_ = {};
    }
    if (retryTimer_ != ASFW::Scheduling::kInvalidTimerToken && deps_.scheduler) {
        deps_.scheduler->Cancel(retryTimer_);
    }
    retryTimer_ = ASFW::Scheduling::kInvalidTimerToken;
}

void BusManagerElectionDriver::Contend(uint32_t generation, uint8_t localNodeId, uint8_t irmNodeId, uint16_t busBase16) noexcept {
    if (!ElectionStillAllowed() || !deps_.asyncController) {
        inFlight_ = false;
        return;
    }

    // Verify generation is still current
    const auto state = deps_.asyncController->GetBusStateSnapshot();
    if (state.generation16 != generation) {
        ASFW_LOG(Controller, "[BM Election] Contention aborted: generation is stale (expected %u, current %u)",
                 generation, state.generation16);
        inFlight_ = false;
        return;
    }
    // Local Loopback Compare-Swap Branching (FW-14)
    if (irmNodeId == localNodeId) {
        ASFW_LOG(Controller, "[BM Election] Local node is IRM; routing CompareSwap through local CSRControl loopback");
        lastElectionPath_ = 1; // Local

        ASFW::Driver::LocalCSRLockResult result;
        if (deps_.localIrmController) {
            result = deps_.localIrmController->CompareSwapBusManagerId(Driver::IRMCSR::kNoBusManagerId, localNodeId);
        } else {
            if (deps_.hardware == nullptr) {
                ASFW_LOG(Controller, "[BM Election] Cannot perform local CompareSwap: hardware interface is null");
                inFlight_ = false;
                HandleCompareSwapResult(generation, localNodeId, irmNodeId, busBase16,
                                        ASFW::Async::AsyncStatus::kHardwareError, 0, false);
                return;
            }
            result = deps_.hardware->CompareSwapLocalIRMResource(
                static_cast<uint32_t>(Driver::IRMCSR::CSRSelector::BusManagerId),
                Driver::IRMCSR::kNoBusManagerId, 
                localNodeId);
        }
        
        if (result.status != ASFW::Driver::LocalCSRLockResult::Status::Success) {
            ASFW_LOG(Controller, "[BM Election] Local CompareSwap failed (status=%d)",
                     static_cast<int>(result.status));
            inFlight_ = false;
            HandleCompareSwapResult(generation, localNodeId, irmNodeId, busBase16,
                                    ASFW::Async::AsyncStatus::kHardwareError, 0, false);
            return;
        }

        // Invoke HandleCompareSwapResult synchronously since it's a local hardware lock sequence
        HandleCompareSwapResult(generation, localNodeId, irmNodeId, busBase16,
                                ASFW::Async::AsyncStatus::kSuccess, result.oldValue,
                                result.compareMatched);
        return;
    }

    lastElectionPath_ = 2; // Remote

    ASFW::Async::CompareSwapParams params{
        .destinationID = ASFW::Driver::ComposeNodeID(busBase16, irmNodeId),
        .addressHigh = ASFW::FW::kCSRRegSpaceHi,
        .addressLow = ASFW::FW::kCSR_BusManagerID,
        .compareValue = Driver::IRMCSR::kNoBusManagerId,
        .swapValue = localNodeId,
        .speedCode = static_cast<uint8_t>(ASFW::FW::Speed::S100)
    };

    ASFW_LOG(Controller, "[BM Election] Issuing CompareSwap to IRM node=0x%04X (phys=%u) for gen=%u",
             params.destinationID, irmNodeId, generation);

    std::weak_ptr<BusManagerElectionDriver> weakSelf = shared_from_this();
    inFlightHandle_ = deps_.asyncController->CompareSwap(params, [weakSelf, generation, localNodeId, irmNodeId, busBase16](ASFW::Async::AsyncStatus status, uint32_t oldValue, bool compareMatched) {
        auto self = weakSelf.lock();
        if (!self) {
            return;
        }
        self->inFlightHandle_ = {}; // clear in-flight handle
        self->HandleCompareSwapResult(generation, localNodeId, irmNodeId, busBase16,
                                      status, oldValue, compareMatched);
    });
}

// Linux splits compare-swap failures two ways: RCODE_SEND_ERROR means "we could
// not send, try again in 1/8 s" (core-card.c:391-398), while any *other*
// non-COMPLETE rcode is read as "maybe the IRM isn't really IRM capable after
// all" and escalates straight to forcing the local node root
// (core-card.c:402-412). We deliberately treat timeout/busy as retryable rather
// than escalating, because the escalation exists here on Apple's route instead:
// the ROM scan verifies every node's IRM by reading and locking
// CHANNELS_AVAILABLE (ROMScanSessionIRM.cpp:84, :165 — Apple
// IOFireWireController.cpp:2691, :2785), and a failure there marks the node a
// bad IRM, which AssignCycleMaster turns into a root change. Do not "fix" this
// into a root force without removing that probe first, or a single unanswered
// lock would trigger two independent root changes.
bool BusManagerElectionDriver::IsRetryableFailure(ASFW::Async::AsyncStatus status) noexcept {
    switch (status) {
    case ASFW::Async::AsyncStatus::kTimeout:
    case ASFW::Async::AsyncStatus::kBusyRetryExhausted:
    case ASFW::Async::AsyncStatus::kHardwareError:
        return true;
    default:
        return false;
    }
}

bool BusManagerElectionDriver::ScheduleTransientRetry(uint32_t generation, uint8_t localNodeId,
                                                      uint8_t irmNodeId,
                                                      uint16_t busBase16) noexcept {
    if (!deps_.scheduler || attemptsThisGeneration_ >= kMaxAttemptsPerGeneration) {
        return false;
    }

    inFlight_ = true;
    inFlightGen_ = generation;
    std::weak_ptr<BusManagerElectionDriver> weakSelf = shared_from_this();
    retryTimer_ = deps_.scheduler->ScheduleAfter(
        kTransientRetryDelayNs,
        [weakSelf, generation, localNodeId, irmNodeId, busBase16]() {
            auto self = weakSelf.lock();
            if (!self) {
                return;
            }
            self->retryTimer_ = ASFW::Scheduling::kInvalidTimerToken;
            if (!self->ElectionStillAllowed() || !self->deps_.asyncController) {
                self->inFlight_ = false;
                return;
            }

            const auto state = self->deps_.asyncController->GetBusStateSnapshot();
            if (state.generation16 != generation) {
                self->inFlight_ = false;
                self->fsm_.IncrementStaleAbortCount();
                ASFW_LOG(Controller,
                         "[BM Election] Transient retry suppressed: generation changed "
                         "from %u to %u",
                         generation, state.generation16);
                return;
            }

            self->attemptedGeneration_ = generation;
            self->attemptsThisGeneration_++;
            ASFW_LOG(Controller,
                     "[BM Election] Retrying CompareSwap for gen=%u (attempt=%u)",
                     generation, static_cast<unsigned>(self->attemptsThisGeneration_));
            self->Contend(generation, localNodeId, irmNodeId, busBase16);
        });

    if (retryTimer_ == ASFW::Scheduling::kInvalidTimerToken) {
        inFlight_ = false;
        return false;
    }
    return true;
}

void BusManagerElectionDriver::HandleCompareSwapResult(
    uint32_t generation, uint8_t localNodeId, uint8_t irmNodeId, uint16_t busBase16,
    ASFW::Async::AsyncStatus status, uint32_t oldValue, bool compareMatched) noexcept {
    if (!active_) {
        return;
    }

    inFlight_ = false;

    // Verify generation is still current
    if (deps_.asyncController) {
        const auto state = deps_.asyncController->GetBusStateSnapshot();
        if (state.generation16 != generation) {
            ASFW_LOG(Controller, "[BM Election] CompareSwap callback ignored: generation is stale (callback gen=%u, current gen=%u)",
                     generation, state.generation16);
            fsm_.IncrementStaleAbortCount();
            return;
        }
    }

    if (status != ASFW::Async::AsyncStatus::kSuccess) {
        ASFW_LOG(Controller, "[BM Election] CompareSwap failed with status %d (%{public}s)",
                 static_cast<int>(status), ASFW::Async::ToString(status));
        // Linux firewire core retries RCODE_SEND_ERROR after 1/8 second
        // (core-card.c:391-398). Our async abstraction coalesces local
        // send/transport failures into these retryable statuses, so perform one
        // generation-safe retry.
        if (IsRetryableFailure(status) &&
            ScheduleTransientRetry(generation, localNodeId, irmNodeId, busBase16)) {
            return;
        }
        if (observer_) {
            observer_->OnBMElectionFailed(generation, status);
        }
        return;
    }

    ElectionOutcome outcome = fsm_.InterpretOldValue(oldValue, localNodeId);
    switch (outcome) {
    case ElectionOutcome::WonBM:
        ASFW_LOG(Controller, "[BM Election] WON Bus Manager election! (oldValue=0x%X, compareMatched=%d)", oldValue, compareMatched);
        if (observer_) {
            observer_->OnLocalWonBM(generation, localNodeId);
        }
        break;
    case ElectionOutcome::IncumbentReestablished:
        ASFW_LOG(Controller, "[BM Election] Re-established BM incumbency.");
        if (observer_) {
            observer_->OnLocalWonBM(generation, localNodeId);
        }
        break;
    case ElectionOutcome::RemoteBM:
        ASFW_LOG(Controller, "[BM Election] Remote node 0x%02X won Bus Manager election (oldValue=0x%X).",
                 fsm_.OwnerId().value_or(0xFF), oldValue);
        if (observer_) {
            observer_->OnRemoteBM(generation, fsm_.OwnerId().value_or(0xFF));
        }
        break;
    default:
        if (observer_) {
            observer_->OnBMElectionFailed(generation, ASFW::Async::AsyncStatus::kLockCompareFail);
        }
        break;
    }
}

} // namespace ASFW::Bus
