// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// IRMBootstrapCoordinator.cpp — see IRMBootstrapCoordinator.hpp

#include "IRMBootstrapCoordinator.hpp"
#include "../../Logging/Logging.hpp"

namespace ASFW::Bus {

void IRMBootstrapCoordinator::OnBusResetStarted(uint32_t /*generation*/) noexcept {
    // Retain state across bus reset until Evaluate inspects the new generation.
}

IRMBootstrapDecision IRMBootstrapCoordinator::Evaluate(const IRMBootstrapInputs& in) noexcept {
    // Invariant check: irmNodeId validity vs hasUsableContender
    const bool irmValid = (in.irmNodeId != Driver::kInvalidPhysicalId);
    if (irmValid != in.hasUsableContender) {
        ASFW_LOG(IRM, "⚠️ [IRM Bootstrap] Invariant warning: irmNodeId=%u but hasUsableContender=%d",
                 in.irmNodeId, in.hasUsableContender ? 1 : 0);
    }

    // RoleMode::ClientOnly is strictly passive: zero mutations or resets
    if (in.roleMode == ASFW::FW::RoleMode::ClientOnly) {
        state_ = IRMBootstrapStateIdle{};
        return IRMBootstrapDecision{.resetRequested = false};
    }

    if (!in.topologyValid) {
        return IRMBootstrapDecision{.resetRequested = false};
    }

    // Pattern match current state using std::visit
    return std::visit([this, &in, irmValid](auto&& state) -> IRMBootstrapDecision {
        using T = std::decay_t<decltype(state)>;

        if constexpr (std::is_same_v<T, IRMBootstrapStateAwaitingGeneration>) {
            if (irmValid) {
                // Success! A valid IRM exists on the bus.
                lastSuccessfulGeneration_ = in.generation;
                lastElectedIrmNodeId_ = in.irmNodeId;
                ASFW_LOG(IRM, "✅ [IRM Bootstrap] Succeeded in gen %u (IRM node=%u). Bootstrap going dormant.",
                         in.generation, in.irmNodeId);
                state_ = IRMBootstrapStateIdle{};
                return IRMBootstrapDecision{.resetRequested = false};
            }

            // Still no IRM! Check if this generation was caused by our bootstrap reset.
            if (in.provenanceResetRequestId == state.resetRequestId) {
                // Our own reset failed to produce an IRM. Suppress further resets to prevent loops.
                ASFW_LOG(IRM, "⚠️ [IRM Bootstrap] Bootstrap reset in gen %u produced no usable IRM. Entering SuppressedFailed.",
                         in.generation);
                state_ = IRMBootstrapStateSuppressedFailed{
                    .failedGeneration = in.generation,
                    .failedResetRequestId = state.resetRequestId,
                    .physicalTopology = in.physicalTopology,
                    .reason = "Zero usable contenders persisted after bootstrap reset"
                };
                return IRMBootstrapDecision{.resetRequested = false};
            }

            // An external reset happened while awaiting generation; fall through to evaluate as fresh attempt.
            state_ = IRMBootstrapStateIdle{};
        }

        if constexpr (std::is_same_v<T, IRMBootstrapStateSuppressedFailed>) {
            if (irmValid) {
                // An external event established a valid IRM.
                lastSuccessfulGeneration_ = in.generation;
                lastElectedIrmNodeId_ = in.irmNodeId;
                state_ = IRMBootstrapStateIdle{};
                return IRMBootstrapDecision{.resetRequested = false};
            }

            // Check if we should re-arm:
            // 1. Physical topology changed materially (device added/removed)
            const bool physicalTopologyChanged = !(in.physicalTopology == state.physicalTopology);
            // 2. An external reset occurred (generation not caused by our failed reset)
            const bool externalResetOccurred = (in.provenanceResetRequestId != state.failedResetRequestId && in.generation > state.failedGeneration);

            if (physicalTopologyChanged || externalResetOccurred) {
                ASFW_LOG(IRM, "🔄 [IRM Bootstrap] Re-arming bootstrap: physicalChanged=%d externalReset=%d",
                         physicalTopologyChanged ? 1 : 0, externalResetOccurred ? 1 : 0);
                state_ = IRMBootstrapStateIdle{};
                // Fall through to Idle evaluation below
            } else {
                // Stay suppressed
                return IRMBootstrapDecision{.resetRequested = false};
            }
        }

        // State is now IRMBootstrapStateIdle
        if (irmValid) {
            // Bus already has a valid IRM; nothing to bootstrap.
            lastSuccessfulGeneration_ = in.generation;
            lastElectedIrmNodeId_ = in.irmNodeId;
            return IRMBootstrapDecision{.resetRequested = false};
        }

        // IRM is absent (zero usable contenders on bus) and role mode is active (IRMResourceHost / FullBusManager).
        // Verify local readiness before promoting wire eligibility:
        if (!in.localIrmCsrHostReady) {
            ASFW_LOG(IRM, "⚠️ [IRM Bootstrap] Local IRM CSR host not ready; bootstrap suppressed");
            return IRMBootstrapDecision{.resetRequested = false};
        }

        const uint64_t reqId = nextResetRequestId_++;
        attemptsCount_++;

        ASFW_LOG(IRM, "🚀 [IRM Bootstrap] Zero usable contenders in gen %u. Requesting bootstrap reset (reqId=%llu, targetRoot=%u)",
                 in.generation, reqId, in.localNodeId);

        state_ = IRMBootstrapStateAwaitingGeneration{
            .triggerGeneration = in.generation,
            .resetRequestId = reqId,
            .physicalTopology = in.physicalTopology,
            .assertedRootHoldoff = true,
            .previousRootHoldoff = in.currentRootHoldOff,
            .requestedAtNs = in.nowNs
        };

        return IRMBootstrapDecision{
            .resetRequested = true,
            .targetRoot = in.localNodeId,
            .setContender = true,
            .rootHoldoff = true,
            .reason = "IRM Bootstrap (Zero Usable Contenders)"
        };
    }, state_);
}

} // namespace ASFW::Bus
