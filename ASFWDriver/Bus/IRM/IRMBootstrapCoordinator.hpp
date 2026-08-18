// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// IRMBootstrapCoordinator.hpp — Pre-Role IRM Bootstrap Coordinator (C++23 std::variant FSM).

#pragma once

#include "../../Common/CSRSpace.hpp"
#include "../../Controller/ControllerTypes.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ASFW::Bus {

/**
 * @brief Stable physical topology signature based on node identities and physical connectivity.
 * Excludes transient role state (root, C/L bits, gap count).
 */
struct PhysicalTopologySignature {
    uint32_t nodeCount{0};
    uint64_t connectivityHash{0};

    bool operator==(const PhysicalTopologySignature&) const = default;
};

struct IRMBootstrapStateIdle {};

struct IRMBootstrapStateAwaitingGeneration {
    uint32_t triggerGeneration{0};
    uint64_t resetRequestId{0};
    PhysicalTopologySignature physicalTopology{};
    bool assertedRootHoldoff{false};
    bool previousRootHoldoff{false};
    uint64_t requestedAtNs{0};
};

struct IRMBootstrapStateSuppressedFailed {
    uint32_t failedGeneration{0};
    uint64_t failedResetRequestId{0};
    PhysicalTopologySignature physicalTopology{};
    const char* reason{"Zero usable contenders persisted after bootstrap reset"};
};

using IRMBootstrapState = std::variant<
    IRMBootstrapStateIdle,
    IRMBootstrapStateAwaitingGeneration,
    IRMBootstrapStateSuppressedFailed
>;

struct IRMBootstrapDecision {
    bool resetRequested{false};
    uint8_t targetRoot{0xFF};
    bool setContender{false};
    bool rootHoldoff{false};
    bool restoreRootHoldoff{false};
    bool rootHoldoffToRestore{false};
    const char* reason{""};
};

struct IRMBootstrapInputs {
    ASFW::FW::RoleMode roleMode{ASFW::FW::RoleMode::ClientOnly};
    bool topologyValid{false};
    uint32_t generation{0};
    uint64_t provenanceResetRequestId{0};
    uint8_t localNodeId{0xFF};
    uint8_t irmNodeId{0xFF};
    uint8_t rootNodeId{0xFF};
    uint8_t gapCount{63};
    bool hasUsableContender{false};
    PhysicalTopologySignature physicalTopology{};
    bool localCmcReady{false};
    bool localIrmCsrHostReady{false};
    bool currentRootHoldOff{false};
    uint64_t nowNs{0};
};

class IRMBootstrapCoordinator {
public:
    IRMBootstrapCoordinator() noexcept = default;
    ~IRMBootstrapCoordinator() noexcept = default;

    [[nodiscard]] IRMBootstrapDecision Evaluate(const IRMBootstrapInputs& in) noexcept;
    void OnBusResetStarted(uint32_t generation) noexcept;

    [[nodiscard]] const IRMBootstrapState& State() const noexcept { return state_; }
    [[nodiscard]] uint32_t LastSuccessfulGeneration() const noexcept { return lastSuccessfulGeneration_; }
    [[nodiscard]] uint8_t LastElectedIrmNodeId() const noexcept { return lastElectedIrmNodeId_; }
    [[nodiscard]] uint32_t AttemptsCount() const noexcept { return attemptsCount_; }

private:
    IRMBootstrapState state_{IRMBootstrapStateIdle{}};
    uint32_t lastSuccessfulGeneration_{0};
    uint8_t lastElectedIrmNodeId_{0xFF};
    uint32_t attemptsCount_{0};
    uint64_t nextResetRequestId_{1};
};

} // namespace ASFW::Bus
