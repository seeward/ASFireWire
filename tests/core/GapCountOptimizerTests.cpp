// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 ASFW Project

#include <gtest/gtest.h>
#include "ASFWDriver/Bus/GapCountOptimizer.hpp"

using namespace ASFW::Driver;

// ============================================================================
// Hop Count Calculation Tests
// ============================================================================

TEST(GapCountOptimizer, CalculateFromHops_SingleNode) {
    // Single node (no hops)
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(0), 63);
}

TEST(GapCountOptimizer, CalculateFromHops_TwoNodes) {
    // 2 nodes = 1 hop
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(1), 5);
}

TEST(GapCountOptimizer, CalculateFromHops_ThreeNodes_RealWorld) {
    // Real-world scenario from FireBug logs:
    // 3 nodes (Mac + FireBug + another device)
    // Root node ID = 2 → max hops = 2
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(2), 7);
}

TEST(GapCountOptimizer, CalculateFromHops_FourNodes) {
    // 4 nodes = 3 hops
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(3), 8);
}

TEST(GapCountOptimizer, CalculateFromHops_FiveNodes) {
    // 5 nodes = 4 hops
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(4), 10);
}

TEST(GapCountOptimizer, CalculateFromHops_MaxTableSize) {
    // Edge of table (25 hops)
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(25), 63);
}

TEST(GapCountOptimizer, CalculateFromHops_BeyondTable) {
    // Beyond table size should clamp to 63
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(30), 63);
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(100), 63);
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(255), 63);
}

// ============================================================================
// Reference table equivalence
// ============================================================================

// P1394a draft 4 table C-2. Both references carry it verbatim; if ours ever
// drifts from either, gap selection stops being wire-compatible. Apple's table
// is IOFireWireController.cpp:3209-3212; Linux's is core-card.c:276-278, which
// stops at 16 hops and treats everything beyond as 63.
TEST(GapCountOptimizer, TableMatchesAppleGapTable) {
    static constexpr uint8_t kAppleGaps[25] = {
        63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40,
        43, 46, 48, 51, 54, 57, 59, 62, 63
    };

    for (uint8_t hops = 0; hops < 25; ++hops) {
        EXPECT_EQ(GapCountOptimizer::CalculateFromHops(hops), kAppleGaps[hops])
            << "hop count " << static_cast<int>(hops);
    }
}

TEST(GapCountOptimizer, TableMatchesLinuxGapCountTableOverItsRange) {
    static constexpr uint8_t kLinuxGaps[16] = {
        63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40
    };

    for (uint8_t hops = 0; hops < 16; ++hops) {
        EXPECT_EQ(GapCountOptimizer::CalculateFromHops(hops), kLinuxGaps[hops])
            << "hop count " << static_cast<int>(hops);
    }
}

// Apple clamps maxHops to 25 before indexing (IOFireWireController.cpp:3300-3302);
// Linux falls back to 63 once max_hops leaves its table (core-card.c:482-485).
// Both land on the conservative maximum, and so must we.
TEST(GapCountOptimizer, OverRangeHopCountsAreConservative) {
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(24), 63);
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(25), 63);
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(63), 63);
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(255), 63);
}

// ============================================================================
// Apple ping-term equivalence
// ============================================================================

namespace {

// IOFireWireController::finishedBusScan() gap selection, transcribed verbatim
// from IOFireWireController.cpp:3290-3333, including the ping term.
uint8_t AppleFinishedBusScanGap(uint8_t maxHops, const uint32_t* pingTimes, uint8_t rootNodeId) {
    static constexpr uint32_t kAppleGaps[26] = {
        63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40,
        43, 46, 48, 51, 54, 57, 59, 62, 63, 63
    };

    uint32_t maxPing = 0;
    for (uint8_t i = 0; i <= rootNodeId; ++i) {
        if (pingTimes[i] > maxPing) {
            maxPing = pingTimes[i];
        }
    }

    if (maxHops > 25) {
        maxHops = 25;
    }
    if (maxPing > 245) {
        maxPing = 245;
    }

    const uint32_t pingGap = (maxPing >= 29) ? kAppleGaps[(maxPing - 20) / 9] : 5;
    const uint32_t hopGap = kAppleGaps[maxHops];

    return static_cast<uint8_t>(hopGap > pingGap ? hopGap : pingGap);
}

} // namespace

// The reason this driver has no ping term. AppleFWOHCI::getPingTimes()
// (AppleFWOHCI559 __text:0x861C) returns a 64-entry inline array at +0xBC0 that
// nothing in the kext ever writes — the `lea` in that three-instruction
// accessor is the only reference to 0xBC0..0xCBF in the whole __text segment,
// and IOKit zeroes instance memory. So Apple's maxPing is always 0, pingGap is
// always 5, and 5 is the table minimum for every hop count >= 1. The ping term
// cannot change the answer on shipping Apple hardware, and this test says so in
// a form that fails if anyone reintroduces it believing otherwise.
TEST(GapCountOptimizer, ApplePingTermIsInertWithTheZeroPingTimesTheFwimSupplies) {
    const uint32_t kZeroPingTimes[64] = {};

    for (uint8_t hops = 1; hops <= 25; ++hops) {
        // rootNodeId only bounds Apple's scan over the (all-zero) array.
        EXPECT_EQ(AppleFinishedBusScanGap(hops, kZeroPingTimes, 63),
                  GapCountOptimizer::CalculateFromHops(hops))
            << "hop count " << static_cast<int>(hops);
    }
}

// And the term is *not* inert if a FWIM ever did fill the array — proof that
// the equivalence above rests on the zeroes, not on the formula.
TEST(GapCountOptimizer, ApplePingTermWouldRaiseTheGapIfAFwimEverFilledTheArray) {
    uint32_t pingTimes[64] = {};
    pingTimes[3] = 200; // (200 - 20) / 9 = 20 -> gaps[20] = 54

    // One hop would otherwise select 5.
    EXPECT_EQ(GapCountOptimizer::CalculateFromHops(1), 5);
    EXPECT_EQ(AppleFinishedBusScanGap(1, pingTimes, 63), 54);
}

// ============================================================================
// Floor invariant
// ============================================================================

// A gap count of 0 is invalid on the wire and 1-4 is below anything either
// reference will select: the table's smallest non-63 entry is 5. Whatever the
// hop count, selection must never fall through that floor.
TEST(GapCountOptimizer, NeverSelectsBelowTheTableFloor) {
    for (uint16_t hops = 0; hops < 300; ++hops) {
        const uint8_t gap = GapCountOptimizer::CalculateFromHops(static_cast<uint8_t>(hops));
        EXPECT_GE(gap, 5) << "hop count " << hops;
        EXPECT_LE(gap, 63) << "hop count " << hops;
    }
}

// ============================================================================
// Gap Consistency Tests
// ============================================================================

TEST(GapCountOptimizer, AreGapsConsistent_Empty) {
    std::vector<uint8_t> gaps = {};
    EXPECT_TRUE(GapCountOptimizer::AreGapsConsistent(gaps));
}

TEST(GapCountOptimizer, AreGapsConsistent_SingleNode) {
    std::vector<uint8_t> gaps = {7};
    EXPECT_TRUE(GapCountOptimizer::AreGapsConsistent(gaps));
}

TEST(GapCountOptimizer, AreGapsConsistent_AllSame) {
    std::vector<uint8_t> gaps = {7, 7, 7};
    EXPECT_TRUE(GapCountOptimizer::AreGapsConsistent(gaps));
}

TEST(GapCountOptimizer, AreGapsConsistent_Default63_RealWorld) {
    // From FireBug logs: all nodes initially have gap=0x3f (63)
    std::vector<uint8_t> gaps = {63, 63, 63};
    EXPECT_TRUE(GapCountOptimizer::AreGapsConsistent(gaps));
}

TEST(GapCountOptimizer, AreGapsConsistent_Mismatch) {
    // Inconsistent gaps (from Apple code comment)
    std::vector<uint8_t> gaps = {7, 63, 7};
    EXPECT_FALSE(GapCountOptimizer::AreGapsConsistent(gaps));
}

TEST(GapCountOptimizer, AreGapsConsistent_TwoNodesDisagree) {
    std::vector<uint8_t> gaps = {7, 8};
    EXPECT_FALSE(GapCountOptimizer::AreGapsConsistent(gaps));
}

// ============================================================================
// Invalid Gap Detection Tests
// ============================================================================

TEST(GapCountOptimizer, HasInvalidGap_Zero) {
    // gap=0 is INVALID per IEEE 1394a
    std::vector<uint8_t> gaps = {0, 0, 0};
    EXPECT_TRUE(GapCountOptimizer::HasInvalidGap(gaps));
}

TEST(GapCountOptimizer, HasInvalidGap_ZeroAmongValid) {
    // Even one gap=0 is invalid
    std::vector<uint8_t> gaps = {7, 0, 7};
    EXPECT_TRUE(GapCountOptimizer::HasInvalidGap(gaps));
}

TEST(GapCountOptimizer, HasInvalidGap_Inconsistent) {
    // Inconsistent gaps are invalid
    std::vector<uint8_t> gaps = {7, 63, 7};
    EXPECT_TRUE(GapCountOptimizer::HasInvalidGap(gaps));
}

TEST(GapCountOptimizer, HasInvalidGap_Valid) {
    // All consistent, non-zero gaps are valid
    std::vector<uint8_t> gaps = {7, 7, 7};
    EXPECT_FALSE(GapCountOptimizer::HasInvalidGap(gaps));
}

// ============================================================================
// ShouldUpdate Tests (Decision Logic)
// ============================================================================

TEST(GapCountOptimizer, ShouldUpdate_Empty) {
    // No nodes → no update
    std::vector<uint8_t> gaps = {};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, 7, 0xFF));
}

TEST(GapCountOptimizer, ShouldUpdate_AlreadyOptimal) {
    // Current gap matches new gap → no update
    std::vector<uint8_t> gaps = {7, 7, 7};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, 7, 0xFF));
}

TEST(GapCountOptimizer, ShouldUpdate_MatchesPrevious) {
    // Current gap matches previous gap (avoid jitter)
    std::vector<uint8_t> gaps = {8, 8, 8};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, 7, 8));
}

TEST(GapCountOptimizer, ShouldUpdate_NeedChange) {
    // Current gap doesn't match new or previous → update
    std::vector<uint8_t> gaps = {63, 63, 63};  // Default
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(gaps, 7, 0xFF));
}

TEST(GapCountOptimizer, ShouldUpdate_Inconsistent_RealWorld) {
    // From Apple code: inconsistent gaps MUST be updated
    std::vector<uint8_t> gaps = {7, 63, 7};
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(gaps, 7, 7));
}

TEST(GapCountOptimizer, ShouldUpdate_Zero_Critical) {
    // gap=0 is CRITICAL ERROR → MUST update
    std::vector<uint8_t> gaps = {0, 0, 0};
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(gaps, 7, 7));
}

TEST(GapCountOptimizer, ShouldUpdate_ZeroAmongConsistent_Critical) {
    // Even if only one node has gap=0 → MUST update
    std::vector<uint8_t> gaps = {7, 0, 7};  // Inconsistent + zero
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(gaps, 7, 7));
}

TEST(GapCountOptimizer, ShouldUpdate_FromDefault63ToOptimal) {
    // Real-world scenario: nodes boot with gap=63, optimize to gap=7
    std::vector<uint8_t> gaps = {63, 63, 63};
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(gaps, 7, 0xFF));
}

TEST(GapCountOptimizer, ShouldUpdate_StableAfterFirstUpdate) {
    // After first update: current=7, new=7, prev=63 → no update (stable)
    std::vector<uint8_t> gaps = {7, 7, 7};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, 7, 63));
}

TEST(GapCountOptimizer, ShouldUpdate_JitterPrevention) {
    // Ping time jitter might change gap 7→8→7
    // If current=7, new=8, prev=7 → should NOT update (matches prev)
    std::vector<uint8_t> gaps = {7, 7, 7};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, 8, 7));
}

// ============================================================================
// Integration Test: Complete Real-World Scenario
// ============================================================================

TEST(GapCountOptimizer, RealWorldScenario_ThreeNodeBus) {
    // Scenario from FireBug logs:
    // - 3 nodes: Mac (node 0), FireBug (node 1), Device (node 2)
    // - Root node ID = 2 → max hops = 2
    // - Initial gaps = [63, 63, 63] (default)
    // - Expected optimal gap = 7

    // Step 1: Calculate optimal gap
    uint8_t maxHops = 2;  // Root node ID
    uint8_t optimalGap = GapCountOptimizer::CalculateFromHops(maxHops);
    EXPECT_EQ(optimalGap, 7);

    // Step 2: Check if update needed (first boot)
    std::vector<uint8_t> currentGaps = {63, 63, 63};
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(currentGaps, optimalGap, 0xFF));

    // Step 3: After update, gaps should be consistent
    std::vector<uint8_t> updatedGaps = {7, 7, 7};
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(updatedGaps, optimalGap, 63));

    // Step 4: Verify no further updates needed
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(updatedGaps, optimalGap, optimalGap));
}

TEST(GapCountOptimizer, RealWorldScenario_GapZeroDetection) {
    // Scenario from kernel logs:
    // - PHY packet 0x00000200 was sent (gap=0, T=1, R=0)
    // - This created invalid state: Self-ID shows gap=0
    // - Must detect and force update

    // Simulate gap=0 in Self-IDs
    std::vector<uint8_t> brokenGaps = {0, 7, 0};  // Node 2 has gap=0 from bad PHY packet

    // Should detect as invalid
    EXPECT_TRUE(GapCountOptimizer::HasInvalidGap(brokenGaps));

    // Should force update
    EXPECT_TRUE(GapCountOptimizer::ShouldUpdate(brokenGaps, 7, 7));
}

TEST(GapCountOptimizer, RealWorldScenario_NoInfiniteLoop) {
    // Ensure that after max attempts, the logic would stop
    // (This test just verifies the gap calculation itself doesn't cause loops)

    std::vector<uint8_t> gaps = {7, 7, 7};
    uint8_t newGap = 7;
    uint8_t prevGap = 7;

    // Should NOT update if already optimal
    EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, newGap, prevGap));

    // Even if called repeatedly, should still return false
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(GapCountOptimizer::ShouldUpdate(gaps, newGap, prevGap));
    }
}
