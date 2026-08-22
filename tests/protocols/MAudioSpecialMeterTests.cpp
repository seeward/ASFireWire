// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "../../ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialMeter.hpp"

namespace ASFW::Audio::BeBoB {
namespace {

using Block = std::array<uint8_t, MAudioSpecialMeterState::kBlockBytes>;

TEST(MAudioSpecialMeterTests, DecodesPeaksAndClockStatusFromFdfByte) {
    Block payload{};
    payload[4] = 0x12;
    payload[5] = 0x34;
    payload[6] = 0x7f;
    payload[7] = 0xff;
    payload[82] = 0x02; // FDF = 48 kHz.
    payload[83] = 0x01; // External sync asserted.

    MAudioSpecialMeterState state{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, state));
    EXPECT_EQ(state.peaks[0], 0x1234);
    EXPECT_EQ(state.peaks[1], 0x7fff);
    EXPECT_EQ(state.detectedSampleRateHz, 48'000U);
    EXPECT_TRUE(state.clockLocked);
    EXPECT_TRUE(state.externalSync);
}

TEST(MAudioSpecialMeterTests, TreatsFFStatusByteAsUnlocked) {
    Block payload{};
    payload[82] = 0xff;

    MAudioSpecialMeterState state{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, state));
    EXPECT_EQ(state.detectedSampleRateHz, 0U);
    EXPECT_FALSE(state.clockLocked);
    EXPECT_FALSE(state.externalSync);
}

// The last peak ends at byte 79, so the status tail must not be read as audio
// and the peak array must not run into it.
TEST(MAudioSpecialMeterTests, CoversAllThirtyEightPeakPoints) {
    Block payload{};
    payload[78] = 0x0a;
    payload[79] = 0x0b;

    MAudioSpecialMeterState state{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, state));
    EXPECT_EQ(state.peaks[37], 0x0a0b);

    size_t covered = 0;
    for (const auto& section : kMAudio1814MeterSections) {
        EXPECT_EQ(section.firstIndex, covered) << section.name;
        covered += section.count;
    }
    EXPECT_EQ(covered, MAudioSpecialMeterState::kPeakCount);
}

TEST(MAudioSpecialMeterTests, RejectsAnythingButTheKnownBlockShape) {
    std::array<uint8_t, MAudioSpecialMeterState::kBlockBytes - 4> shortPayload{};
    MAudioSpecialMeterState state{};
    EXPECT_FALSE(DecodeMAudioSpecialMeter(shortPayload, state));
    EXPECT_EQ(MAudioRateFromFdf(7), 0U);
}

// The first block seen has no predecessor, so every event byte looks like a
// change. Acting on it would fake a knob turn on every connect.
TEST(MAudioSpecialMeterTests, IgnoresEventsInTheFirstBlock) {
    Block payload{};
    payload[0] = 0x01;
    payload[1] = 0x01;
    payload[2] = 0x02;
    payload[3] = 0x01;

    MAudioSpecialMeterState state{};
    MAudio1814RotaryDelta deltas{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, state, &deltas));
    EXPECT_FALSE(deltas.Any());
    EXPECT_FALSE(state.hardwareSwitch);
    EXPECT_EQ(state.rotaries[0], 0);
}

// Field 1 is one detent up, field 2 is one detent down
// (TRotaryControl::RotaryValueChanged @ 0xc2ba).
TEST(MAudioSpecialMeterTests, IntegratesRotaryDetentsInBothDirections) {
    MAudioSpecialMeterState state{};
    Block idle{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state));

    MAudio1814RotaryDelta deltas{};
    Block down{};
    down[1] = 0x02; // headphone knob 1, one detent down
    ASSERT_TRUE(DecodeMAudioSpecialMeter(down, state, &deltas));
    EXPECT_EQ(deltas.detents[0], -1);
    EXPECT_EQ(state.rotaries[0], 0xFC00U);

    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state, &deltas));
    EXPECT_FALSE(deltas.Any());
    EXPECT_EQ(state.rotaries[0], 0xFC00U);

    Block up{};
    up[1] = 0x01;
    ASSERT_TRUE(DecodeMAudioSpecialMeter(up, state, &deltas));
    EXPECT_EQ(deltas.detents[0], 1);
    EXPECT_EQ(state.rotaries[0], 0);
}

TEST(MAudioSpecialMeterTests, RotaryEventCountersWrapAndHoldSteadyStateSilent) {
    MAudioSpecialMeterState state{};
    Block idle{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state));

    // An event counter must not clamp at a fictional physical maximum.
    state.rotaries[2] = 0xFC00U;
    Block up{};
    up[3] = 0x01; // assignable knob, wraps after 0xffff
    MAudio1814RotaryDelta deltas{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(up, state, &deltas));
    EXPECT_EQ(deltas.detents[2], 1);
    EXPECT_EQ(state.rotaries[2], 0U);

    // Holding the same field across polls is one event, not one per poll.
    ASSERT_TRUE(DecodeMAudioSpecialMeter(up, state, &deltas));
    EXPECT_FALSE(deltas.Any());
}

TEST(MAudioSpecialMeterTests, TogglesTheSwitchOnPressOnly) {
    MAudioSpecialMeterState state{};
    Block idle{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state));

    Block pressed{};
    pressed[0] = 0x01;
    ASSERT_TRUE(DecodeMAudioSpecialMeter(pressed, state));
    EXPECT_TRUE(state.hardwareSwitch);

    // Release does not toggle back; the next press does.
    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state));
    EXPECT_TRUE(state.hardwareSwitch);
    ASSERT_TRUE(DecodeMAudioSpecialMeter(pressed, state));
    EXPECT_FALSE(state.hardwareSwitch);
}

// The vendor masks each event field to two bits, so bits above bit 1 must not
// stop a detent being seen.
TEST(MAudioSpecialMeterTests, MasksEventFieldsToTwoBitsLikeTheVendor) {
    MAudioSpecialMeterState state{};
    Block idle{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(idle, state));

    Block payload{};
    payload[2] = 0xF1; // high bits set, field still reads 1
    MAudio1814RotaryDelta deltas{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, state, &deltas));
    EXPECT_EQ(deltas.detents[1], 1);
}

} // namespace
} // namespace ASFW::Audio::BeBoB
