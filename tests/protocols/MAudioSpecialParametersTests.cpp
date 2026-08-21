// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "../../ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialParameters.hpp"

namespace ASFW::Audio::BeBoB {
namespace {

TEST(MAudioSpecialParametersTests, StartsWithAudibleLabDerivedRouting) {
    const MAudioSpecialParameterImage image;

    EXPECT_EQ(image.ValueAt(37), 0x00000009U); // Stream 1/2 -> Mix 1/2; 3/4 -> Mix 3/4.
    EXPECT_EQ(image.ValueAt(38), 0x00020001U); // Headphones follow their mixer pairs.
    EXPECT_EQ(image.ValueAt(39), 0x00000000U); // Analog outputs take mixer, not aux.
}

TEST(MAudioSpecialParametersTests, UpdatesOnlyTheNamedOutputLevelQuadlet) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    ASSERT_TRUE(image.Apply(MAudio1814ControlId::AnalogOutput12Level, -16384, changedIndex));
    EXPECT_EQ(changedIndex, 2U);
    EXPECT_EQ(image.ValueAt(changedIndex), 0xC000C000U);
    EXPECT_EQ(image.ControlValue(MAudio1814ControlId::AnalogOutput12Level), -16384);
}

TEST(MAudioSpecialParametersTests, EncodesSelectorsAndRejectsOutOfDomainValues) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    ASSERT_TRUE(image.Apply(MAudio1814ControlId::Headphone34Source, 2, changedIndex));
    EXPECT_EQ(changedIndex, 38U);
    EXPECT_EQ(image.ValueAt(38), 0x00040001U);
    EXPECT_EQ(image.ControlValue(MAudio1814ControlId::Headphone34Source), 2);
    EXPECT_FALSE(image.Apply(MAudio1814ControlId::Headphone12Source, 3, changedIndex));
    EXPECT_FALSE(image.Apply(MAudio1814ControlId::AnalogOutput12Level, 1, changedIndex));
}

TEST(MAudioSpecialParametersTests, PreservesOtherMixerBitsWhenChangingOneMask) {
    MAudioSpecialParameterImage image;
    size_t changedIndex = 0;

    ASSERT_TRUE(image.Apply(MAudio1814ControlId::StreamMixerSendMask, 0x06, changedIndex));
    EXPECT_EQ(changedIndex, 37U);
    EXPECT_EQ(image.ValueAt(37), 0x00000006U);
    EXPECT_FALSE(image.Apply(MAudio1814ControlId::PhysicalMixerSendMask, 0x00040000,
                             changedIndex));
}

} // namespace
} // namespace ASFW::Audio::BeBoB
