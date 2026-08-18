// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// IRMBootstrapCoordinatorTests.cpp — Unit tests for Pre-Role IRM Bootstrap Coordinator.

#include <gtest/gtest.h>
#include "Bus/IRM/IRMBootstrapCoordinator.hpp"

namespace {

using ASFW::Bus::IRMBootstrapCoordinator;
using ASFW::Bus::IRMBootstrapDecision;
using ASFW::Bus::IRMBootstrapInputs;
using ASFW::Bus::IRMBootstrapStateAwaitingGeneration;
using ASFW::Bus::IRMBootstrapStateIdle;
using ASFW::Bus::IRMBootstrapStateSuppressedFailed;
using ASFW::Bus::PhysicalTopologySignature;
using ASFW::FW::RoleMode;

class IRMBootstrapCoordinatorTests : public ::testing::Test {
protected:
    IRMBootstrapCoordinator coordinator_{};
};

TEST_F(IRMBootstrapCoordinatorTests, ClientOnlyModeRemainsStrictlyPassive) {
    IRMBootstrapInputs in{
        .roleMode = RoleMode::ClientOnly,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true,
    };

    const auto decision = coordinator_.Evaluate(in);
    EXPECT_FALSE(decision.resetRequested);
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateIdle>(coordinator_.State()));
    EXPECT_EQ(coordinator_.AttemptsCount(), 0);
}

TEST_F(IRMBootstrapCoordinatorTests, FailClosedWhenLocalCmcOrCsrHostNotReady) {
    // 1. localCmcReady = false
    IRMBootstrapInputs in1{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = false,
        .localIrmCsrHostReady = true,
    };
    EXPECT_FALSE(coordinator_.Evaluate(in1).resetRequested);

    // 2. localIrmCsrHostReady = false
    IRMBootstrapInputs in2{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = false,
    };
    EXPECT_FALSE(coordinator_.Evaluate(in2).resetRequested);
}

TEST_F(IRMBootstrapCoordinatorTests, IRMResourceHostBootstrapsWhenZeroUsableContenders) {
    IRMBootstrapInputs in{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false, // e.g. Node 1 has C=1, L=0 (inactive) and Node 2 has C=0, L=1
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true,
        .currentRootHoldOff = false,
        .nowNs = 100'000'000ULL
    };

    const auto decision = coordinator_.Evaluate(in);
    EXPECT_TRUE(decision.resetRequested);
    EXPECT_EQ(decision.targetRoot, 0);
    EXPECT_TRUE(decision.setContender);
    EXPECT_TRUE(decision.rootHoldoff);
    EXPECT_STREQ(decision.reason, "IRM Bootstrap (Zero Usable Contenders)");

    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateAwaitingGeneration>(coordinator_.State()));
    EXPECT_EQ(coordinator_.AttemptsCount(), 1);
}

TEST_F(IRMBootstrapCoordinatorTests, BootstrapSucceedsWhenNextGenEdictsLocalIRMAndRestoresRHB) {
    // Gen 1: Bootstrap requested with initial RHB = false
    IRMBootstrapInputs inGen1{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true,
        .currentRootHoldOff = false
    };
    const auto d1 = coordinator_.Evaluate(inGen1);
    ASSERT_TRUE(d1.resetRequested);
    EXPECT_TRUE(d1.rootHoldoff);

    // Gen 2: Erupted from bootstrap reset (local node is IRM=0)
    IRMBootstrapInputs inGen2{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 2,
        .provenanceResetRequestId = 1,
        .localNodeId = 0,
        .irmNodeId = 0,
        .hasUsableContender = true,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true
    };
    const auto d2 = coordinator_.Evaluate(inGen2);
    EXPECT_FALSE(d2.resetRequested);
    EXPECT_TRUE(d2.restoreRootHoldoff);
    EXPECT_FALSE(d2.rootHoldoffToRestore); // Restores to previous RHB (false)
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateIdle>(coordinator_.State()));
    EXPECT_EQ(coordinator_.LastSuccessfulGeneration(), 2);
    EXPECT_EQ(coordinator_.LastElectedIrmNodeId(), 0);
}

TEST_F(IRMBootstrapCoordinatorTests, BootstrapSuppressesWhenResetFailsToProduceIRMAndRestoresRHB) {
    // Gen 1: Bootstrap requested
    IRMBootstrapInputs inGen1{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true,
        .currentRootHoldOff = false
    };
    coordinator_.Evaluate(inGen1);

    // Gen 2: Still IRM=none from our reset (reqId=1)
    IRMBootstrapInputs inGen2{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 2,
        .provenanceResetRequestId = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true
    };
    const auto d2 = coordinator_.Evaluate(inGen2);
    EXPECT_FALSE(d2.resetRequested);
    EXPECT_TRUE(d2.restoreRootHoldoff);
    EXPECT_FALSE(d2.rootHoldoffToRestore);
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateSuppressedFailed>(coordinator_.State()));

    // Gen 3: Same physical topology, no external reset -> remains suppressed
    IRMBootstrapInputs inGen3 = inGen2;
    inGen3.generation = 3;
    const auto d3 = coordinator_.Evaluate(inGen3);
    EXPECT_FALSE(d3.resetRequested);
    EXPECT_FALSE(d3.restoreRootHoldoff);
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateSuppressedFailed>(coordinator_.State()));
}

TEST_F(IRMBootstrapCoordinatorTests, ExternalResetRearmsSuppressedBootstrap) {
    // Gen 1: Bootstrap
    IRMBootstrapInputs in{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true
    };
    coordinator_.Evaluate(in);

    // Gen 2: Failed
    in.generation = 2;
    in.provenanceResetRequestId = 1;
    coordinator_.Evaluate(in);
    ASSERT_TRUE(std::holds_alternative<IRMBootstrapStateSuppressedFailed>(coordinator_.State()));

    // Gen 3: External reset occurred (provenanceResetRequestId = 0, generation = 3)
    in.generation = 3;
    in.provenanceResetRequestId = 0;
    const auto d3 = coordinator_.Evaluate(in);
    EXPECT_TRUE(d3.resetRequested); // Re-armed!
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateAwaitingGeneration>(coordinator_.State()));
}

TEST_F(IRMBootstrapCoordinatorTests, PhysicalTopologyChangeRearmsSuppressedBootstrap) {
    // Gen 1: Bootstrap
    IRMBootstrapInputs in{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 0xFF,
        .hasUsableContender = false,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true
    };
    coordinator_.Evaluate(in);

    // Gen 2: Failed
    in.generation = 2;
    in.provenanceResetRequestId = 1;
    coordinator_.Evaluate(in);
    ASSERT_TRUE(std::holds_alternative<IRMBootstrapStateSuppressedFailed>(coordinator_.State()));

    // Gen 3: Physical topology changed (device plugged in -> nodeCount = 4, hash changed)
    in.generation = 3;
    in.physicalTopology = {.nodeCount = 4, .connectivityHash = 0x5678};
    const auto d3 = coordinator_.Evaluate(in);
    EXPECT_TRUE(d3.resetRequested); // Re-armed!
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateAwaitingGeneration>(coordinator_.State()));
}

TEST_F(IRMBootstrapCoordinatorTests, SubsequentIRMLossReevaluatesBootstrap) {
    // Start with valid IRM
    IRMBootstrapInputs in{
        .roleMode = RoleMode::IRMResourceHost,
        .topologyValid = true,
        .generation = 1,
        .localNodeId = 0,
        .irmNodeId = 2,
        .hasUsableContender = true,
        .physicalTopology = {.nodeCount = 3, .connectivityHash = 0x1234},
        .localCmcReady = true,
        .localIrmCsrHostReady = true
    };
    const auto d1 = coordinator_.Evaluate(in);
    EXPECT_FALSE(d1.resetRequested);
    EXPECT_EQ(coordinator_.LastSuccessfulGeneration(), 1);

    // Gen 2: IRM lost externally (e.g. peripheral power cycled and returned C=0)
    in.generation = 2;
    in.irmNodeId = 0xFF;
    in.hasUsableContender = false;
    const auto d2 = coordinator_.Evaluate(in);
    EXPECT_TRUE(d2.resetRequested);
    EXPECT_TRUE(std::holds_alternative<IRMBootstrapStateAwaitingGeneration>(coordinator_.State()));
}

} // namespace
