// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "../../ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialMeter.hpp"

namespace ASFW::Audio::BeBoB {
namespace {

TEST(MAudioSpecialMeterTests, DecodesPeaksAndClockStatusFromFdfByte) {
    std::array<uint8_t, MAudioSpecialMeterState::kBlockBytes> payload{};
    payload[4] = 0x12;
    payload[5] = 0x34;
    payload[6] = 0x7f;
    payload[7] = 0xff;
    payload[82] = 0x02; // FDF = 48 kHz.

    MAudioSpecialMeterState state{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, 48'000, state));
    EXPECT_EQ(state.peaks[0], 0x1234);
    EXPECT_EQ(state.peaks[1], 0x7fff);
    EXPECT_EQ(state.detectedSampleRateHz, 48'000U);
    EXPECT_TRUE(state.clockLocked);
}

TEST(MAudioSpecialMeterTests, TreatsFFStatusByteAsUnlocked) {
    std::array<uint8_t, MAudioSpecialMeterState::kBlockBytes> payload{};
    payload[82] = 0xff;

    MAudioSpecialMeterState state{};
    ASSERT_TRUE(DecodeMAudioSpecialMeter(payload, 48'000, state));
    EXPECT_EQ(state.detectedSampleRateHz, 0U);
    EXPECT_FALSE(state.clockLocked);
}

TEST(MAudioSpecialMeterTests, RejectsAnythingButTheKnownBlockShape) {
    std::array<uint8_t, MAudioSpecialMeterState::kBlockBytes - 4> shortPayload{};
    MAudioSpecialMeterState state{};
    EXPECT_FALSE(DecodeMAudioSpecialMeter(shortPayload, 48'000, state));
    EXPECT_EQ(MAudioRateFromFdf(7), 0U);
}

} // namespace
} // namespace ASFW::Audio::BeBoB
