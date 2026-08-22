// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include <gtest/gtest.h>

#include "Audio/Families/BeBoB/BeBoBCaptureChannelMap.hpp"

namespace {

using ASFW::Audio::Families::BeBoBProbe::CaptureChannelMapFromProbe;
using ASFW::Protocols::AVC::Probe::ChannelPosition;
using ASFW::Protocols::AVC::Probe::ChannelSection;
using ASFW::Protocols::AVC::Probe::IsochronousPlugModel;

constexpr uint8_t kLineSectionType = 0x03;
constexpr uint8_t kMidiSectionType = 0x0a;

[[nodiscard]] ChannelSection Section(
    uint8_t type, std::initializer_list<ChannelPosition> positions) {
    return {.type = type, .positions = positions};
}

} // namespace

TEST(BeBoBCaptureChannelMapTests, ReordersPlanarWireSectionToLogicalChannels) {
    // The stream slots are planar L1, L2, R1, R2 while the advertised section
    // locations put them in CoreAudio order L1, R1, L2, R2.
    IsochronousPlugModel capture{};
    capture.channelSections.push_back(Section(kLineSectionType, {
        {0, 0}, {1, 2}, {2, 1}, {3, 3},
    }));

    const auto map = CaptureChannelMapFromProbe(capture, 4, 5);

    ASSERT_EQ(map.slotCount, 4u);
    EXPECT_EQ(map.channelCount, 4u);
    EXPECT_EQ(map.SlotFor(0), 0u);
    EXPECT_EQ(map.SlotFor(1), 2u);
    EXPECT_EQ(map.SlotFor(2), 1u);
    EXPECT_EQ(map.SlotFor(3), 3u);
    EXPECT_TRUE(map.FitsWithin(4, 5));
}

TEST(BeBoBCaptureChannelMapTests, MidiSectionDoesNotConsumeAPcmChannel) {
    IsochronousPlugModel capture{};
    capture.channelSections.push_back(Section(kLineSectionType, {
        {0, 0}, {1, 1},
    }));
    capture.channelSections.push_back(Section(kMidiSectionType, {
        {2, 0},
    }));
    capture.channelSections.push_back(Section(kLineSectionType, {
        {3, 0}, {4, 1},
    }));

    const auto map = CaptureChannelMapFromProbe(capture, 4, 5);

    ASSERT_EQ(map.slotCount, 4u);
    EXPECT_EQ(map.SlotFor(0), 0u);
    EXPECT_EQ(map.SlotFor(1), 1u);
    EXPECT_EQ(map.SlotFor(2), 3u);
    EXPECT_EQ(map.SlotFor(3), 4u);
}

TEST(BeBoBCaptureChannelMapTests, InvalidOrIncompleteEvidenceFailsClosedToIdentity) {
    IsochronousPlugModel duplicateLocation{};
    duplicateLocation.channelSections.push_back(Section(kLineSectionType, {
        {0, 0}, {1, 0},
    }));
    EXPECT_TRUE(CaptureChannelMapFromProbe(duplicateLocation, 2, 2).IsIdentity());

    IsochronousPlugModel missingSectionType{};
    missingSectionType.channelSections.push_back({
        .positions = {{0, 0}, {1, 1}},
    });
    EXPECT_TRUE(CaptureChannelMapFromProbe(missingSectionType, 2, 2).IsIdentity());

    IsochronousPlugModel outOfRangeSlot{};
    outOfRangeSlot.channelSections.push_back(Section(kLineSectionType, {
        {0, 0}, {2, 1},
    }));
    EXPECT_TRUE(CaptureChannelMapFromProbe(outOfRangeSlot, 2, 2).IsIdentity());
}

TEST(BeBoBCaptureChannelMapTests, IdentityReplyKeepsTheDecoderFastPath) {
    IsochronousPlugModel capture{};
    capture.channelSections.push_back(Section(kLineSectionType, {
        {0, 0}, {1, 1},
    }));

    const auto map = CaptureChannelMapFromProbe(capture, 2, 3);
    EXPECT_TRUE(map.IsIdentity());
    EXPECT_TRUE(map.FitsWithin(2, 3));
}
