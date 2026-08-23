// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialConsoleLayout.hpp"
#include "ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialParameters.hpp"

namespace ASFW::Audio::BeBoB {
namespace {

TEST(MAudioSpecialConsoleLayoutTests, AdatAndSpdifAreBothValidSemanticLayouts) {
    AudioSemanticConsoleLayoutSnapshot adat{};
    AudioSemanticConsoleLayoutSnapshot spdif{};
    ASSERT_TRUE(BuildMAudioSpecialConsoleLayout(MAudioDigitalFormat::ADAT, adat));
    ASSERT_TRUE(BuildMAudioSpecialConsoleLayout(MAudioDigitalFormat::SPDIF, spdif));
    adat.topologyRevision = 1;
    spdif.topologyRevision = 1;

    EXPECT_TRUE(ValidateAudioSemanticConsoleLayout(adat).has_value());
    EXPECT_TRUE(ValidateAudioSemanticConsoleLayout(spdif).has_value());
    EXPECT_EQ(adat.stripCount, 15U);
    EXPECT_EQ(spdif.stripCount, 12U);
    EXPECT_EQ(adat.crosspointCount, 20U);
    EXPECT_EQ(spdif.crosspointCount, 14U);
}

TEST(MAudioSpecialConsoleLayoutTests, CrosspointsCarryTheActualDriverControlBinding) {
    AudioSemanticConsoleLayoutSnapshot snapshot{};
    ASSERT_TRUE(BuildMAudioSpecialConsoleLayout(MAudioDigitalFormat::ADAT, snapshot));
    snapshot.topologyRevision = 1;

    const auto& analog12ToMain12 = snapshot.crosspoints[0];
    EXPECT_EQ(analog12ToMain12.sourceStripId, 1U);
    EXPECT_EQ(analog12ToMain12.destinationBus, AudioSemanticConsoleBus::Main12);
    EXPECT_EQ(analog12ToMain12.controlId,
              MakeMAudio1814ControlId(MAudio1814ControlGroup::PhysicalMixerSendMask, 0));
    EXPECT_EQ(analog12ToMain12.enabledMask, 1U);

    const auto& adat12ToMain34 = snapshot.crosspoints[9];
    EXPECT_EQ(adat12ToMain34.sourceStripId, 11U);
    EXPECT_EQ(adat12ToMain34.destinationBus, AudioSemanticConsoleBus::Main34);
    EXPECT_EQ(adat12ToMain34.enabledMask, 1U << 12U);
}

} // namespace
} // namespace ASFW::Audio::BeBoB
