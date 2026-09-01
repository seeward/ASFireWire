// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 ASFW Project
//
// RootClaimRetryBudgetTests.cpp — bounds on root-forcing bus policy.

#include "Bus/BusManager/RootClaimRetryBudget.hpp"
#include "Bus/TopologyTypes.hpp"
#include "gtest/gtest.h"

using ASFW::Bus::RootClaimRetryBudget;

TEST(RootClaimRetryBudgetTests, AllowsExactlyTheConfiguredNumberOfAttempts) {
    RootClaimRetryBudget budget{3};

    EXPECT_TRUE(budget.TryConsume(0xABCDu));
    EXPECT_TRUE(budget.TryConsume(0xABCDu));
    EXPECT_TRUE(budget.TryConsume(0xABCDu));
    EXPECT_FALSE(budget.TryConsume(0xABCDu));
    EXPECT_EQ(budget.AttemptsOnCurrentTopology(), 3u);
}

TEST(RootClaimRetryBudgetTests, DefaultsToLinuxFiveResetBound) {
    // Linux bounds the equivalent bus-manager reset with `card->bm_retries++ < 5`
    // (core-card.c:493).
    RootClaimRetryBudget budget{};
    EXPECT_EQ(budget.MaxAttempts(), 5u);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(budget.TryConsume(1u)) << "attempt " << i;
    }
    EXPECT_FALSE(budget.TryConsume(1u));
}

TEST(RootClaimRetryBudgetTests, ExhaustedBudgetStaysExhaustedOnTheSameTopology) {
    RootClaimRetryBudget budget{1};

    EXPECT_TRUE(budget.TryConsume(7u));
    EXPECT_FALSE(budget.TryConsume(7u));
    EXPECT_FALSE(budget.TryConsume(7u));
    EXPECT_FALSE(budget.TryConsume(7u));
}

TEST(RootClaimRetryBudgetTests, ANewTopologyRefillsTheBudget) {
    RootClaimRetryBudget budget{2};

    EXPECT_TRUE(budget.TryConsume(11u));
    EXPECT_TRUE(budget.TryConsume(11u));
    EXPECT_FALSE(budget.TryConsume(11u));

    EXPECT_TRUE(budget.TryConsume(12u));
    EXPECT_EQ(budget.AttemptsOnCurrentTopology(), 1u);
}

// The whole point of the budget: a root claim resets the bus, producing a new
// generation whose topology differs only in who is root. If that refilled the
// budget, a device that re-asserts root-hold-off would be chased forever. The
// key excludes rootNodeId precisely so this test can hold.
TEST(RootClaimRetryBudgetTests, ForcingRootDoesNotRefillTheBudget) {
    ASFW::Driver::TopologySnapshot topo{};
    topo.nodeCount = 2;
    topo.localNodeId = 0;
    topo.irmNodeId = 0;
    topo.rootNodeId = 1;
    topo.physical.nodes.resize(2);
    topo.physical.nodes[0].physicalId = 0;
    topo.physical.nodes[0].linkActive = true;
    topo.physical.nodes[0].contender = true;
    topo.physical.nodes[1].physicalId = 1;
    topo.physical.nodes[1].linkActive = true;

    RootClaimRetryBudget budget{2};

    EXPECT_TRUE(budget.TryConsume(ASFW::Driver::StableTopologyKey(topo)));

    // We won: the reset landed and we are root in the new generation.
    topo.rootNodeId = 0;
    topo.generation = 9;
    EXPECT_TRUE(budget.TryConsume(ASFW::Driver::StableTopologyKey(topo)));

    // The device took root back. Same physical bus — the budget must be spent.
    topo.rootNodeId = 1;
    topo.generation = 10;
    EXPECT_FALSE(budget.TryConsume(ASFW::Driver::StableTopologyKey(topo)));
}

TEST(RootClaimRetryBudgetTests, ResetStartsAFreshBudget) {
    RootClaimRetryBudget budget{1};

    EXPECT_TRUE(budget.TryConsume(5u));
    EXPECT_FALSE(budget.TryConsume(5u));

    budget.Reset();
    EXPECT_TRUE(budget.TryConsume(5u));
}
