// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// LocalIRMResourceController.cpp — see LocalIRMResourceController.hpp

#include "LocalIRMResourceController.hpp"
#include "../CSR/BroadcastChannelCSR.hpp"
#include "../../Logging/Logging.hpp"

namespace ASFW::Bus {

using namespace ASFW::Driver::IRMCSR;

LocalIRMResourceController::LocalIRMResourceController(ASFW::Driver::HardwareInterface& hw,
                                                       BroadcastChannelCSR& broadcastChannel) noexcept
    : hw_(hw), broadcastChannel_(broadcastChannel), csr_(hw) {}

void LocalIRMResourceController::OnBusResetStarted(uint32_t generation) noexcept {
    snapshot_.generation = generation;
    
    if (hw_.InitialIRMRegistersProgrammed()) {
        snapshot_.state = LocalIRMResourceState::InitialRegistersProgrammed;
        snapshot_.initialRegistersProgrammed = true;
    } else {
        // Fall back to Disabled or add a new failed state?
        // Let's use the Failed state if we have one, or just mark it in diagnostics.
        snapshot_.state = LocalIRMResourceState::ProbeFailed; // Reuse for now
        snapshot_.initialRegistersProgrammed = false;
    }

    snapshot_.localIsIRM = false;
    snapshot_.activeProbeAttempted = false;
    snapshot_.activeProbeSucceeded = false;
    
    // BROADCAST_CHANNEL resets to unimplemented/invalid on bus reset
    broadcastChannel_.ResetImplementedInvalid();
}

void LocalIRMResourceController::OnTopologyReady(uint32_t generation,
                                                 uint8_t localNodeId,
                                                 uint8_t irmNodeId,
                                                 bool roleAllowsIRMHost) noexcept {
    snapshot_.generation = generation;
    snapshot_.localNodeId = localNodeId;
    snapshot_.irmNodeId = irmNodeId;

    if (!roleAllowsIRMHost) {
        snapshot_.state = LocalIRMResourceState::Disabled;
        broadcastChannel_.ResetImplementedInvalid();
        ASFW_LOG(IRM, "[LocalIRM] gen=%u disabled by role policy (local=%u irm=%u)",
                 generation, localNodeId, irmNodeId);
        return;
    }

    if (localNodeId != irmNodeId) {
        snapshot_.state = LocalIRMResourceState::NotLocalIRM;
        snapshot_.localIsIRM = false;
        broadcastChannel_.ResetImplementedInvalid();
        // Worth stating positively: allocations this generation are wire
        // transactions to another node, not local CSR operations.
        ASFW_LOG(IRM, "[LocalIRM] gen=%u local=%u is NOT the IRM (irm=%u); "
                      "isochronous allocation goes over the wire",
                 generation, localNodeId, irmNodeId);
        return;
    }

    // Local node is IRM.
    //
    // This is not a software election result. The bus elected us because, after
    // Self-ID, our physical node ID is the highest among contender-capable
    // nodes that also transmitted link-active Self-ID. From this point the OHCI
    // CSR engine hosts BUS_MANAGER_ID, BANDWIDTH_AVAILABLE, and
    // CHANNELS_AVAILABLE_* autonomously for remote read/lock requests. Software
    // only validates the active ledger state via local CSRControl and exposes
    // the software-owned BROADCAST_CHANNEL.
    snapshot_.localIsIRM = true;
    snapshot_.state = LocalIRMResourceState::InitializingActiveResources;

    // Probe active OHCI autonomous resources
    if (!ProbeActiveResources()) {
        broadcastChannel_.ResetImplementedInvalid();
        return;
    }

    // Mark BROADCAST_CHANNEL valid only after the OHCI channel resource shows
    // channel 31 reserved.
    // cross-validated with Linux: core-card.c:258 Apple: IOFireWireIRM.cpp:301
    if ((snapshot_.channelsAvailableHi & 0x1u) == 0) {
        broadcastChannel_.MarkValidChannel31();
    } else {
        broadcastChannel_.ResetImplementedInvalid();
    }
}

void LocalIRMResourceController::OnTopologyInvalid(uint32_t generation) noexcept {
    snapshot_.generation = generation;
    snapshot_.state = LocalIRMResourceState::Disabled;
    snapshot_.localIsIRM = false;
    broadcastChannel_.ResetImplementedInvalid();
}

bool LocalIRMResourceController::ProbeActiveResources() noexcept {
    snapshot_.activeProbeAttempted = true;
    
    // Probe all four core IRM resources using "no-change" Compare-Swap.
    //
    // Important: this is local software talking to its own OHCI CSR engine via
    // CSRData/CSRCompareData/CSRControl. It does not imply that ASFW can observe
    // every remote node's resource allocation request; those remote read/lock
    // transactions are normally completed autonomously by OHCI on the wire.
    //
    // The no-change CAS lets diagnostics discover the current ledger state
    // without blindly overwriting resources already allocated by remote nodes.
    auto rBM = csr_.ProbeBusManagerIdNoChange(kNoBusManagerId);
    auto rBW = csr_.ProbeBandwidthNoChange(kInitialBandwidthAvailable);
    auto rCHi = csr_.ProbeChannelsHiNoChange(kInitialChannelsAvailableHi);
    auto rCLo = csr_.ProbeChannelsLoNoChange(kInitialChannelsAvailableLo);

    if (rBM.status != ASFW::Driver::LocalCSRLockResult::Status::Success ||
        rBW.status != ASFW::Driver::LocalCSRLockResult::Status::Success ||
        rCHi.status != ASFW::Driver::LocalCSRLockResult::Status::Success ||
        rCLo.status != ASFW::Driver::LocalCSRLockResult::Status::Success) {
        
        snapshot_.state = LocalIRMResourceState::ProbeFailed;
        snapshot_.activeProbeSucceeded = false;

        ASFW_LOG_ERROR(IRM,
                       "[LocalIRM] gen=%u CSR probe FAILED bm=%u bw=%u chHi=%u chLo=%u",
                       snapshot_.generation, static_cast<uint32_t>(rBM.status),
                       static_cast<uint32_t>(rBW.status), static_cast<uint32_t>(rCHi.status),
                       static_cast<uint32_t>(rCLo.status));

        if (rBM.status == ASFW::Driver::LocalCSRLockResult::Status::Timeout ||
            rBW.status == ASFW::Driver::LocalCSRLockResult::Status::Timeout ||
            rCHi.status == ASFW::Driver::LocalCSRLockResult::Status::Timeout ||
            rCLo.status == ASFW::Driver::LocalCSRLockResult::Status::Timeout) {
            snapshot_.lastCsrStatus = 1; // Timeout
        } else {
            snapshot_.lastCsrStatus = 2; // HardwareUnavailable
        }
        return false;
    }

    // Store read/old values for diagnostics
    snapshot_.busManagerId = rBM.oldValue;
    snapshot_.bandwidthAvailable = rBW.oldValue;
    snapshot_.channelsAvailableHi = rCHi.oldValue;
    snapshot_.channelsAvailableLo = rCLo.oldValue;
    
    snapshot_.activeProbeSucceeded = true;
    snapshot_.lastCsrStatus = 0; // OK

    // Decide between ReadyDefaults or ReadyChanged
    if (rBM.compareMatched && rBW.compareMatched && rCHi.compareMatched && rCLo.compareMatched) {
        snapshot_.state = LocalIRMResourceState::ReadyDefaults;
    } else {
        snapshot_.state = LocalIRMResourceState::ReadyChanged;
    }

    // The ledger as it stands at the start of this generation, before any
    // client has allocated. This is the only record of what a bus reset
    // actually left in the OHCI IRM registers: bw=0x1333 with all channels but
    // 31 free is the expected reload from the Initial* registers, and anything
    // else is either a real allocation by another node or a bring-up defect.
    // One line per topology-ready, never a hot path.
    ASFW_LOG(IRM,
             "[LocalIRM] gen=%u local node %u IS the IRM: bm=0x%08x bw=0x%08x (%u units) "
             "chHi=0x%08x chLo=0x%08x state=%{public}s",
             snapshot_.generation, snapshot_.localNodeId, snapshot_.busManagerId,
             snapshot_.bandwidthAvailable, snapshot_.bandwidthAvailable,
             snapshot_.channelsAvailableHi, snapshot_.channelsAvailableLo,
             snapshot_.state == LocalIRMResourceState::ReadyDefaults ? "ReadyDefaults"
                                                                     : "ReadyChanged");

    return true;
}

void LocalIRMResourceController::Disable() noexcept {
    snapshot_.state = LocalIRMResourceState::Disabled;
    snapshot_.localIsIRM = false;
    snapshot_.activeProbeAttempted = false;
    snapshot_.activeProbeSucceeded = false;
    broadcastChannel_.ResetImplementedInvalid();
}

ASFW::Driver::LocalCSRLockResult LocalIRMResourceController::CompareSwapBusManagerId(uint32_t compareValue,
                                                                                    uint32_t newValue) noexcept {
    return csr_.CompareSwapBusManagerId(compareValue, newValue);
}

} // namespace ASFW::Bus
