// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Families/BeBoB/MAudio/MAudioInternalTxTiming.hpp"
#include "ASFWDriver/Common/TimingUtils.hpp"

#include <variant>

namespace {

using namespace ASFW::Audio::Families::BeBoB::MAudio;

constexpr StartEpoch kEpoch{91};
constexpr uint32_t kSytWindowTicks = 16U * ASFW::Timing::kTicksPerCycle;

[[nodiscard]] constexpr uint32_t SytTicks(const uint16_t syt) {
    return ((syt >> 12) & 0x0FU) * ASFW::Timing::kTicksPerCycle +
           (syt & 0x0FFFU);
}

[[nodiscard]] constexpr uint32_t LeadTicks(const uint16_t syt,
                                           const uint32_t transmitCycle) {
    return (SytTicks(syt) -
            (transmitCycle & 0x0FU) * ASFW::Timing::kTicksPerCycle +
            kSytWindowTicks) %
           kSytWindowTicks;
}

TEST(MAudioInternalTxTimingTests, ArmsOnlyWhenGeometryMatchesTheRateFamily) {
    InternalTxTiming timing;

    EXPECT_FALSE(timing.Arm(kEpoch, 44'100, 16));
    EXPECT_TRUE(std::holds_alternative<InternalTxTimingFailed>(timing.State()));
    EXPECT_FALSE(timing.IsArmed());

    EXPECT_TRUE(timing.Arm(kEpoch, 44'100, kInternalTxSytInterval));
    EXPECT_TRUE(std::holds_alternative<InternalTxTimingRunning>(timing.State()));
    EXPECT_EQ(timing.TransferDelayTicks(), 13'162U);

    EXPECT_TRUE(timing.Arm(kEpoch, 48'000, kInternalTxSytInterval));
    EXPECT_EQ(timing.TransferDelayTicks(), 12'800U);
}

TEST(MAudioInternalTxTimingTests, FortyEightKRetainsThePriorThreeDataOneNoDataCadence) {
    InternalTxTiming timing;
    ASSERT_TRUE(timing.Arm(kEpoch, 48'000, kInternalTxSytInterval));

    constexpr bool expectedData[] = {true, true, true, false, true};
    constexpr uint16_t expectedOffsets[] = {0, 1'024, 2'048,
                                             ::ASFW::Protocols::Audio::AMDTP::kNoSytOffset,
                                             0};
    for (uint64_t index = 0; index < std::size(expectedData); ++index) {
        InternalTxPacketPlan plan{};
        ASSERT_TRUE(timing.PreviewNextPacket(plan));
        EXPECT_EQ(plan.sequence, index);
        EXPECT_EQ(plan.cadenceCycle, index);
        EXPECT_EQ(plan.isData, expectedData[index]);
        EXPECT_EQ(plan.dataBlocks, expectedData[index] ? kInternalTxSytInterval : 0U);
        EXPECT_EQ(plan.sytOffsetTicks, expectedOffsets[index]);
        EXPECT_TRUE(timing.CommitPacket(plan, plan.isData));
    }
}

TEST(MAudioInternalTxTimingTests, FortyFourKUsesExactRationalPacketCadence) {
    InternalTxTiming timing;
    ASSERT_TRUE(timing.Arm(kEpoch, 44'100, kInternalTxSytInterval));

    uint32_t dataPackets = 0;
    bool havePreviousSyt = false;
    uint32_t previousSytTicks = 0;
    for (uint32_t cycle = 0; cycle < 640; ++cycle) {
        InternalTxPacketPlan plan{};
        ASSERT_TRUE(timing.PreviewNextPacket(plan));
        ASSERT_EQ(plan.cadenceCycle, cycle);
        if (plan.isData) {
            ASSERT_EQ(plan.dataBlocks, kInternalTxSytInterval);
            ASSERT_NE(plan.sytOffsetTicks,
                      ::ASFW::Protocols::Audio::AMDTP::kNoSytOffset);
            const uint32_t sytTicks = SytTicks(ComputeInternalTxSyt(
                plan.sytOffsetTicks, cycle, timing.TransferDelayTicks()));
            if (havePreviousSyt) {
                const uint32_t delta =
                    (sytTicks + kSytWindowTicks - previousSytTicks) % kSytWindowTicks;
                EXPECT_TRUE(delta == 4'458U || delta == 4'459U)
                    << "cycle " << cycle << " delta=" << delta;
            }
            previousSytTicks = sytTicks;
            havePreviousSyt = true;
            ++dataPackets;
        } else {
            EXPECT_EQ(plan.dataBlocks, 0U);
            EXPECT_EQ(plan.sytOffsetTicks,
                      ::ASFW::Protocols::Audio::AMDTP::kNoSytOffset);
        }
        ASSERT_TRUE(timing.CommitPacket(plan, plan.isData));
    }
    EXPECT_EQ(dataPackets, 441U);
}

TEST(MAudioInternalTxTimingTests, SytLeadUsesTheRateDependentTransferDelay) {
    for (const uint32_t rate : {44'100U, 48'000U}) {
        InternalTxTiming timing;
        ASSERT_TRUE(timing.Arm(kEpoch, rate, kInternalTxSytInterval));
        for (uint32_t transmitCycle : {0U, 7U, 15U, 16U, 4'383U}) {
            InternalTxPacketPlan plan{};
            ASSERT_TRUE(timing.PreviewNextPacket(plan));
            if (plan.isData) {
                const uint16_t syt = ComputeInternalTxSyt(
                    plan.sytOffsetTicks, transmitCycle, timing.TransferDelayTicks());
                EXPECT_EQ(LeadTicks(syt, transmitCycle),
                          plan.sytOffsetTicks + timing.TransferDelayTicks());
            }
            ASSERT_TRUE(timing.CommitPacket(plan, plan.isData));
        }
    }
}

TEST(MAudioInternalTxTimingTests, NoDataFallbackStillAdvancesTheSharedCadence) {
    InternalTxTiming fallback;
    InternalTxTiming reference;
    ASSERT_TRUE(fallback.Arm(kEpoch, 44'100, kInternalTxSytInterval));
    ASSERT_TRUE(reference.Arm(kEpoch, 44'100, kInternalTxSytInterval));

    for (uint32_t cycle = 0; cycle < 128; ++cycle) {
        InternalTxPacketPlan fallbackPlan{};
        InternalTxPacketPlan referencePlan{};
        ASSERT_TRUE(fallback.PreviewNextPacket(fallbackPlan));
        ASSERT_TRUE(reference.PreviewNextPacket(referencePlan));
        EXPECT_EQ(fallbackPlan.isData, referencePlan.isData);
        EXPECT_EQ(fallbackPlan.sytOffsetTicks, referencePlan.sytOffsetTicks);
        ASSERT_TRUE(fallback.CommitPacket(fallbackPlan, false));
        ASSERT_TRUE(reference.CommitPacket(referencePlan, referencePlan.isData));
    }
}

TEST(MAudioInternalTxTimingTests, RejectsAStalePreviewAndDisarmsCleanly) {
    InternalTxTiming timing;
    ASSERT_TRUE(timing.Arm(kEpoch, 48'000, kInternalTxSytInterval));

    InternalTxPacketPlan plan{};
    ASSERT_TRUE(timing.PreviewNextPacket(plan));
    EXPECT_TRUE(timing.CommitPacket(plan, plan.isData));
    EXPECT_FALSE(timing.CommitPacket(plan, plan.isData));

    timing.Disarm();
    EXPECT_TRUE(std::holds_alternative<InternalTxTimingStopped>(timing.State()));
    EXPECT_FALSE(timing.IsArmed());
    EXPECT_FALSE(timing.PreviewNextPacket(plan));
}

} // namespace
