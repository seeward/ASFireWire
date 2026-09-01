#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "ASFWDriver/Bus/BusManager.hpp"
#include "ASFWDriver/Controller/ControllerConfig.hpp"
#include "ASFWDriver/Controller/BringupOverrides.hpp"

using namespace ASFW::Driver;

namespace {

uint32_t MakeBaseSelfID(const uint8_t phyId, const uint8_t gapCount, const bool contender = false) {
    uint32_t quadlet = 0x80000000U;
    quadlet |= (static_cast<uint32_t>(phyId) & 0x3FU) << 24U;
    quadlet |= 1U << 22U;
    quadlet |= (static_cast<uint32_t>(gapCount) & 0x3FU) << 16U;
    quadlet |= 0x2U << 14U;
    if (contender) {
        quadlet |= 1U << 11U;
    }
    return quadlet;
}

TopologySnapshot MakeTopology(const uint8_t localNodeId,
                              const uint8_t irmNodeId,
                              const uint8_t busDiameterHops) {
    TopologySnapshot topology{};
    topology.localNodeId = localNodeId;
    topology.irmNodeId = irmNodeId;
    topology.physical.busDiameterHops = busDiameterHops;
    return topology;
}

TopologyNodeRecord MakeNode(const uint8_t physicalId, const bool contender, const bool linkActive = true) {
    TopologyNodeRecord node{};
    node.physicalId = physicalId;
    node.contender = contender;
    node.linkActive = linkActive;
    return node;
}

} // namespace

TEST(BusManagerGapOptimizationTests, ControllerConfigDefaultsKeepCycleMasterDelegationOptIn) {
    // IOFireWireController only delegates when its provider carries the
    // "DelegateCycleMaster" property (IOFireWireController.cpp:1047), so the
    // default posture is to keep root/cycle-master duty local.
    ControllerConfig config{};
    EXPECT_FALSE(config.allowCycleMasterEligibility);
    EXPECT_FALSE(config.delegateCycleMaster);

    const ControllerConfig defaultConfig = ControllerConfig::MakeDefault();
    EXPECT_FALSE(defaultConfig.allowCycleMasterEligibility);
    EXPECT_FALSE(defaultConfig.delegateCycleMaster);
}

TEST(BusManagerGapOptimizationTests, DefaultBringupDoesNotDelegateRootToPeerContender) {
    ControllerConfig config{};
    BusManager busManager;
    ApplyBringupOverrides(config, &busManager);

    EXPECT_TRUE(config.allowCycleMasterEligibility);
    EXPECT_FALSE(busManager.GetConfig().delegateCycleMaster);

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 1U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
    };

    // Local is already root and IRM: Apple's simple-BM phase has nothing to do,
    // and the delegation heuristic is gated off.
    const auto command = busManager.AssignCycleMaster(topology, BusManager::BusScanEvidence{});
    EXPECT_FALSE(command.has_value());
}

TEST(BusManagerGapOptimizationTests, OptedInDelegationHandsRootToPeerContender) {
    ControllerConfig config{};
    config.delegateCycleMaster = true;

    BusManager busManager;
    ApplyBringupOverrides(config, &busManager);

    EXPECT_TRUE(busManager.GetConfig().delegateCycleMaster);

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 1U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
    };

    const auto command = busManager.AssignCycleMaster(topology, BusManager::BusScanEvidence{});
    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(command->forceRootNodeID.has_value());
    ASSERT_TRUE(command->setContender.has_value());
    EXPECT_EQ(*command->forceRootNodeID, 0U);
    EXPECT_FALSE(*command->setContender);
}

TEST(BusManagerGapOptimizationTests, LocalIRMRemoteRootWithoutPeerContenderForcesLocalRoot) {
    BusManager busManager;

    TopologySnapshot topology{};
    topology.localNodeId = 0U;
    topology.rootNodeId = 2U;
    topology.irmNodeId = 0U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, false, false),
        MakeNode(2U, false),
    };

    const auto command = busManager.AssignCycleMaster(topology, BusManager::BusScanEvidence{});

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(command->forceRootNodeID.has_value());
    ASSERT_TRUE(command->setContender.has_value());
    EXPECT_EQ(*command->forceRootNodeID, 0U);
    EXPECT_TRUE(*command->setContender);
}

TEST(BusManagerGapOptimizationTests, SimpleBusManagerClaimsRootEvenWhenAPeerContenderExists) {
    // IOFireWireController::finishedBusScan() (IOFireWireController.cpp:3262-3362)
    // runs no contender survey: once it knows local is IRM and no remote node
    // advertises BMC, it asserts local root-hold-off and resets. A lower-numbered
    // peer contender must not suppress that, or a bus whose root is a
    // non-contender never acquires a cycle master.
    BusManager busManager;

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 2U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
        MakeNode(2U, false),
    };

    const auto command = busManager.AssignCycleMaster(topology, BusManager::BusScanEvidence{});

    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(command->forceRootNodeID.has_value());
    ASSERT_TRUE(command->setContender.has_value());
    EXPECT_EQ(*command->forceRootNodeID, 1U);
    EXPECT_TRUE(*command->setContender);
}

TEST(BusManagerGapOptimizationTests, UnscannedContenderIsNotARootCandidate) {
    // Apple only treats a node as a root candidate when it holds a scan record
    // for it — `if( fScans[i] )` at IOFireWireController.cpp:2372. Node 0 below
    // is a Self-ID contender whose Config ROM never read, so delegation must not
    // hand it the bus even though delegation is enabled.
    BusManager busManager;
    busManager.SetDelegateMode(true);

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 1U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
    };

    BusManager::BusScanEvidence evidence{};
    evidence.scanned.assign(ASFW::Driver::kMaxPhysicalIds, false);
    evidence.scanned[1U] = true; // local scanned, node 0 never answered

    EXPECT_FALSE(busManager.AssignCycleMaster(topology, evidence).has_value());
}

TEST(BusManagerGapOptimizationTests, ScannedContenderRemainsARootCandidate) {
    BusManager busManager;
    busManager.SetDelegateMode(true);

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 1U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
    };

    BusManager::BusScanEvidence evidence{};
    evidence.scanned.assign(ASFW::Driver::kMaxPhysicalIds, false);
    evidence.scanned[0U] = true;
    evidence.scanned[1U] = true;

    const auto command = busManager.AssignCycleMaster(topology, evidence);
    ASSERT_TRUE(command.has_value());
    ASSERT_TRUE(command->forceRootNodeID.has_value());
    EXPECT_EQ(*command->forceRootNodeID, 0U);
}

TEST(BusManagerGapOptimizationTests, GapMismatchIsVisibleWithoutAnyAuthorityGate) {
    // HasGapCountMismatch() is what lets a node with no retool authority still
    // perform Apple's reset-free gap-63 correction (IOFireWireController.cpp:2139-2151).
    EXPECT_TRUE(BusManager::HasGapCountMismatch(
        {MakeBaseSelfID(0U, 10U), MakeBaseSelfID(1U, 20U)}));
    EXPECT_FALSE(BusManager::HasGapCountMismatch(
        {MakeBaseSelfID(0U, 10U), MakeBaseSelfID(1U, 10U)}));
    EXPECT_FALSE(BusManager::HasGapCountMismatch({}));
}

TEST(BusManagerGapOptimizationTests, RemoteBusManagerCapabilitySuppressesSimpleBusManagerRootClaim) {
    // Apple gates the whole simple-BM block on !fBusMgr, where fBusMgr means
    // "some remote node advertised BMC in its bus info block"
    // (IOFireWireController.cpp:2972-2974). A BM-capable peer owns bus policy.
    BusManager busManager;

    TopologySnapshot topology{};
    topology.localNodeId = 1U;
    topology.rootNodeId = 2U;
    topology.irmNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
        MakeNode(2U, false),
    };

    const auto command = busManager.AssignCycleMaster(topology, BusManager::BusScanEvidence{.remoteBusManagerCapable = true});
    EXPECT_FALSE(command.has_value());
}

TEST(BusManagerGapOptimizationTests, InconsistentObservedBaseGapsForceConservative63) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    const auto topology = MakeTopology(1U, 1U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 10U), MakeBaseSelfID(1U, 20U)});

    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->reason, BusManager::GapDecisionReason::MismatchForce63);
    EXPECT_EQ(decision->gapCount, 63U);
}

TEST(BusManagerGapOptimizationTests, ObservedZeroGapRetoolsToCurrentTargetGap) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology, {MakeBaseSelfID(0U, 0U), MakeBaseSelfID(1U, 0U)});

    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->reason, BusManager::GapDecisionReason::ZeroObservedGap);
    EXPECT_EQ(decision->gapCount, 10U);
}

TEST(BusManagerGapOptimizationTests, ObservedDefault63GapWithUnknownHistoryRetoolsToCurrentTargetGap) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->reason, BusManager::GapDecisionReason::TargetGap);
    EXPECT_EQ(decision->gapCount, 10U);
}

TEST(BusManagerGapOptimizationTests, TwoNodeLocalRootSkipsTargetGapOptimization) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    auto topology = MakeTopology(1U, 1U, 1U);
    topology.rootNodeId = 1U;
    topology.physical.nodes = {
        MakeNode(0U, true),
        MakeNode(1U, true),
    };

    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    EXPECT_FALSE(decision.has_value());
}

TEST(BusManagerGapOptimizationTests, ObservedGapsMatchingConfirmedGapNeedNoAction) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);
    busManager.NoteStableGapObserved(63U);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    EXPECT_FALSE(decision.has_value());
}

TEST(BusManagerGapOptimizationTests, ObservedGapsMatchingTargetGapNeedNoAction) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 10U), MakeBaseSelfID(1U, 10U)});

    EXPECT_FALSE(decision.has_value());
}

TEST(BusManagerGapOptimizationTests, ForcedGapDifferentFromPreviousReturnsDecision) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);
    busManager.SetForcedGapCount(21U);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    ASSERT_TRUE(decision.has_value());
    EXPECT_EQ(decision->reason, BusManager::GapDecisionReason::ForcedGap);
    EXPECT_EQ(decision->gapCount, 21U);
}

TEST(BusManagerGapOptimizationTests, NonManagerNodeSkipsGapOptimization) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);

    const auto topology = MakeTopology(0U, 1U, 4U);
    const auto decision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 10U), MakeBaseSelfID(1U, 20U)});

    EXPECT_FALSE(decision.has_value());
}

TEST(BusManagerGapOptimizationTests, FailedDispatchDoesNotAdvanceConfirmedGap) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);
    busManager.SetForcedGapCount(21U);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto initialDecision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    ASSERT_TRUE(initialDecision.has_value());
    ASSERT_EQ(initialDecision->reason, BusManager::GapDecisionReason::ForcedGap);
    EXPECT_EQ(initialDecision->gapCount, 21U);

    busManager.NoteGapResetIssued(21U, BusManager::GapDecisionReason::ForcedGap);
    busManager.ClearInFlightGapReset();

    const auto retryDecision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    ASSERT_TRUE(retryDecision.has_value());
    EXPECT_EQ(retryDecision->reason, BusManager::GapDecisionReason::ForcedGap);
    EXPECT_EQ(retryDecision->gapCount, 21U);
}

TEST(BusManagerGapOptimizationTests, StableAcceptedGapCommitsOnlyAfterConsistentObservation) {
    BusManager busManager;
    busManager.SetGapOptimizationEnabled(true);
    busManager.SetForcedGapCount(21U);

    const auto topology = MakeTopology(0U, 0U, 4U);
    const auto initialDecision =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});

    ASSERT_TRUE(initialDecision.has_value());
    busManager.NoteGapResetIssued(21U, BusManager::GapDecisionReason::ForcedGap);

    const auto beforeStableCommit =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 63U), MakeBaseSelfID(1U, 63U)});
    ASSERT_TRUE(beforeStableCommit.has_value());
    EXPECT_EQ(beforeStableCommit->reason, BusManager::GapDecisionReason::ForcedGap);

    busManager.NoteStableGapObserved(21U);

    const auto afterStableCommit =
        busManager.EvaluateGapPolicy(topology,
                                     {MakeBaseSelfID(0U, 21U), MakeBaseSelfID(1U, 21U)});
    EXPECT_FALSE(afterStableCommit.has_value());
}
