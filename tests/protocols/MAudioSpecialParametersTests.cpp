// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "../../ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialParameters.hpp"

#include <set>

namespace ASFW::Audio::BeBoB {
namespace {

uint32_t Id(MAudio1814ControlGroup group, uint32_t index) {
    return MakeMAudio1814ControlId(group, index);
}

// The crate asserts this same invariant over its eighteen OFFSET_RANGEs
// (special.rs, `offset_ranges`). If a range is ever mistyped, this is what
// catches it: the window is write-only, so an overlap would silently corrupt a
// neighbouring control with no error anywhere.
TEST(MAudioSpecialParametersTests, RangesTileTheWindowExactlyOnce) {
    std::array<int, MAudioSpecialParameterImage::kImageBytes> coverage{};
    for (const auto& group : kMAudio1814ControlGroups) {
        const size_t width =
            group.kind == MAudio1814ControlKind::Level ||
                    group.kind == MAudio1814ControlKind::Balance
                ? group.count * sizeof(int16_t)
                : sizeof(uint32_t);
        ASSERT_LE(group.byteOffset + width, coverage.size()) << group.name;
        for (size_t i = 0; i < width; ++i) {
            ++coverage[group.byteOffset + i];
        }
    }
    for (size_t i = 0; i < coverage.size(); ++i) {
        EXPECT_EQ(coverage[i], 1) << "byte 0x" << std::hex << i << " covered "
                                  << coverage[i] << " times";
    }
}

TEST(MAudioSpecialParametersTests, ControlIdsAreUnique) {
    std::set<uint32_t> seen;
    size_t total = 0;
    for (const auto& group : kMAudio1814ControlGroups) {
        for (uint32_t index = 0; index < group.count; ++index) {
            EXPECT_TRUE(seen.insert(Id(group.group, index)).second);
            ++total;
        }
    }
    EXPECT_EQ(total, kMAudio1814ControlCount);
    EXPECT_EQ(total, 78U);
}

TEST(MAudioSpecialParametersTests, StartsWithAudibleLabDerivedRouting) {
    const MAudioSpecialParameterImage image;

    EXPECT_EQ(image.QuadletAt(0x94 / 4), 0x00000009U); // Stream 1/2 -> Mix 1/2; 3/4 -> Mix 3/4.
    EXPECT_EQ(image.QuadletAt(0x98 / 4), 0x00020001U); // Headphones follow their mixer pairs.
    EXPECT_EQ(image.QuadletAt(0x9C / 4), 0x00000000U); // Analog outputs take mixer, not aux.
    EXPECT_EQ(image.QuadletAt(0x90 / 4), 0x00000000U); // No physical input into the mixer.
}

// A zero-filled window means unity on every level field, which would leave all
// eighteen physical inputs summing into the aux bus and therefore into the
// headphones. The crate's MaudioSpecialAuxParameters::default is the reference.
TEST(MAudioSpecialParametersTests, MutesEverythingButAnalogOneTwoOnTheAuxBus) {
    const MAudioSpecialParameterImage image;

    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxAnalogGain, 0)), 0);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxAnalogGain, 1)), 0);
    for (uint32_t i = 2; i < 8; ++i) {
        EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxAnalogGain, i)),
                  MAudioSpecialParameterImage::kLevelMin)
            << "aux analog " << i;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxSpdifGain, i)),
                  MAudioSpecialParameterImage::kLevelMin);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxAdatGain, i)),
                  MAudioSpecialParameterImage::kLevelMin);
    }

    // Mixer-side gains stay at unity: that path is gated by the send masks.
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::MixerAnalogGain, 7)), 0);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::MixerStreamGain, 0)), 0);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AnalogOutputVolume, 0)), 0);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::HeadphoneVolume, 3)), 0);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AuxOutputVolume, 0)), 0);
}

// Vendor factory default, FWSettingsLevels::ResetToFactorySettings @ 0x21bc4:
// pan -255 / +255 through SetInputPan's -128 multiplier is +32640 / -32640, so
// each pair's quadlet reads 0x7F808080.
TEST(MAudioSpecialParametersTests, SeedsBalancesHardPannedTheWayTheVendorDoes) {
    const MAudioSpecialParameterImage image;

    for (size_t offset = 0x40; offset < 0x64; offset += 4) {
        EXPECT_EQ(image.QuadletAt(offset / 4), 0x7F808080U)
            << "balance quadlet 0x" << std::hex << offset;
    }
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::MixerAnalogBalance, 0)),
              MAudioSpecialParameterImage::kBalanceHardPannedFirst);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::MixerAnalogBalance, 1)),
              MAudioSpecialParameterImage::kBalanceHardPannedSecond);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::MixerAdatBalance, 6)),
              MAudioSpecialParameterImage::kBalanceHardPannedFirst);
}

TEST(MAudioSpecialParametersTests, WritesOneChannelWithoutDisturbingItsPair) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputVolume, 0), -16384,
                            changedIndex));
    EXPECT_EQ(changedIndex, 0x08 / 4);
    EXPECT_EQ(image.QuadletAt(changedIndex), 0xC0000000U);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AnalogOutputVolume, 0)), -16384);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::AnalogOutputVolume, 1)), 0);

    // The second channel of the pair shares the quadlet, which is the unit the
    // device is written in.
    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputVolume, 1), -16384,
                            changedIndex));
    EXPECT_EQ(changedIndex, 0x08 / 4);
    EXPECT_EQ(image.QuadletAt(changedIndex), 0xC000C000U);

    // Channels 3/4 are the next quadlet along.
    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputVolume, 2), -256,
                            changedIndex));
    EXPECT_EQ(changedIndex, 0x0C / 4);
    EXPECT_EQ(image.QuadletAt(0x0C / 4), 0xFF000000U);
}

TEST(MAudioSpecialParametersTests, PlacesEveryGroupAtItsDocumentedOffset) {
    struct Expectation {
        MAudio1814ControlGroup group;
        uint32_t index;
        size_t quadlet;
    };
    // One control from each level/balance group, checked against §6 of the RE notes.
    constexpr Expectation kExpectations[] = {
        {MAudio1814ControlGroup::MixerStreamGain, 3, 0x04 / 4},
        {MAudio1814ControlGroup::MixerAnalogGain, 7, 0x1C / 4},
        {MAudio1814ControlGroup::MixerSpdifGain, 1, 0x20 / 4},
        {MAudio1814ControlGroup::MixerAdatGain, 7, 0x30 / 4},
        {MAudio1814ControlGroup::AuxOutputVolume, 1, 0x34 / 4},
        {MAudio1814ControlGroup::HeadphoneVolume, 3, 0x3C / 4},
        {MAudio1814ControlGroup::MixerSpdifBalance, 1, 0x50 / 4},
        {MAudio1814ControlGroup::MixerAdatBalance, 7, 0x60 / 4},
        {MAudio1814ControlGroup::AuxStreamGain, 3, 0x68 / 4},
        {MAudio1814ControlGroup::AuxAnalogGain, 7, 0x78 / 4},
        {MAudio1814ControlGroup::AuxSpdifGain, 1, 0x7C / 4},
        {MAudio1814ControlGroup::AuxAdatGain, 7, 0x8C / 4},
    };
    for (const auto& expectation : kExpectations) {
        MAudioSpecialParameterImage image;
        size_t changedIndex = 0;
        ASSERT_TRUE(image.Apply(Id(expectation.group, expectation.index), -1, changedIndex));
        EXPECT_EQ(changedIndex, expectation.quadlet)
            << MAudio1814GroupInfo(expectation.group)->name << " " << expectation.index;
        EXPECT_EQ(image.ControlValue(Id(expectation.group, expectation.index)), -1);
    }
}

TEST(MAudioSpecialParametersTests, EncodesSelectorsAndRejectsOutOfDomainValues) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    // Headphone pair 2 is the high half of 0x98; one-hot within a three-bit field.
    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::HeadphoneSource, 1), 2, changedIndex));
    EXPECT_EQ(changedIndex, 0x98 / 4);
    EXPECT_EQ(image.QuadletAt(0x98 / 4), 0x00040001U);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::HeadphoneSource, 1)), 2);
    EXPECT_EQ(image.ControlValue(Id(MAudio1814ControlGroup::HeadphoneSource, 0)), 0);

    // Analog output pair source is one bit per pair, 0 = mixer, 1 = aux.
    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputSource, 1), 1, changedIndex));
    EXPECT_EQ(changedIndex, 0x9C / 4);
    EXPECT_EQ(image.QuadletAt(0x9C / 4), 0x00000002U);

    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::HeadphoneSource, 0), 3, changedIndex));
    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputSource, 0), 2, changedIndex));
    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputVolume, 0), 1, changedIndex));
    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::AnalogOutputVolume, 4), 0, changedIndex));
    EXPECT_FALSE(image.Apply(0xDEAD00U, 0, changedIndex));
}

TEST(MAudioSpecialParametersTests, PreservesOtherMixerBitsWhenChangingOneMask) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::StreamMixerSendMask, 0), 0x06,
                            changedIndex));
    EXPECT_EQ(changedIndex, 0x94 / 4);
    EXPECT_EQ(image.QuadletAt(0x94 / 4), 0x00000006U);

    // Analog pair 1 -> mixer pair 1 (bit 0), ADAT pair 1 -> mixer pair 2 (bit 12),
    // S/PDIF -> mixer pair 1 (bit 16). See RE notes §6.
    ASSERT_TRUE(image.Apply(Id(MAudio1814ControlGroup::PhysicalMixerSendMask, 0), 0x00011001,
                            changedIndex));
    EXPECT_EQ(changedIndex, 0x90 / 4);
    EXPECT_EQ(image.QuadletAt(0x90 / 4), 0x00011001U);

    // Bit 18 and above is outside the eighteen-bit physical mask.
    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::PhysicalMixerSendMask, 0), 0x00040000,
                             changedIndex));
    EXPECT_FALSE(image.Apply(Id(MAudio1814ControlGroup::StreamMixerSendMask, 0), 0x10,
                             changedIndex));
}

TEST(MAudioSpecialParametersTests, SerialisesTheWholeWindowInWireOrder) {
    MAudioSpecialParameterImage image;
    const auto bytes = image.Bytes();
    ASSERT_EQ(bytes.size(), MAudioSpecialParameterImage::kImageBytes);
    ASSERT_EQ(bytes.size(), 160U);

    // 0x94 = 0x00000009 big-endian.
    EXPECT_EQ(bytes[0x94], 0x00);
    EXPECT_EQ(bytes[0x95], 0x00);
    EXPECT_EQ(bytes[0x96], 0x00);
    EXPECT_EQ(bytes[0x97], 0x09);
    // 0x40 = 0x7F808080 big-endian.
    EXPECT_EQ(bytes[0x40], 0x7F);
    EXPECT_EQ(bytes[0x41], 0x80);
    EXPECT_EQ(bytes[0x42], 0x80);
    EXPECT_EQ(bytes[0x43], 0x80);
}

} // namespace
} // namespace ASFW::Audio::BeBoB
