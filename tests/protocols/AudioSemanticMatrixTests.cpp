// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>

#include "ASFWDriver/Audio/Shared/Topology/IAudioSemanticMatrix.hpp"
#include "ASFWDriver/Audio/Protocols/DICE/Focusrite/SPro24DspSemanticMatrix.hpp"

namespace ASFW::Audio {
namespace {

AudioSemanticMatrixSnapshot ValidMatrix() {
    AudioSemanticMatrixSnapshot snapshot{};
    snapshot.deviceKind = 0x44494345; // semantic device-family identity only.
    snapshot.topologyRevision = 1;
    snapshot.inputCount = 2;
    snapshot.outputCount = 2;
    snapshot.coefficientMaximum = 65535;
    snapshot.gainLaw = AudioSemanticMatrixGainLaw::LinearNormalized;
    snapshot.inputs[0] = {101, AudioSemanticSignalKind::AnalogLine, 1, 101,
                          AudioSemanticMatrixChannelRole::Mono};
    snapshot.inputs[1] = {102, AudioSemanticSignalKind::HostStream, 1, 102,
                          AudioSemanticMatrixChannelRole::Left};
    snapshot.outputs[0] = {201, AudioSemanticSignalKind::Headphone, 1, 201,
                           AudioSemanticMatrixChannelRole::Mono,
                           AudioSemanticMatrixOutputRole::MonitorMix};
    snapshot.outputs[1] = {202, AudioSemanticSignalKind::AnalogLine, 1, 202,
                           AudioSemanticMatrixChannelRole::Mono,
                           AudioSemanticMatrixOutputRole::MonitorMix};
    snapshot.coefficients[0] = 65535;
    return snapshot;
}

TEST(AudioSemanticMatrixTests, AcceptsBoundedSemanticMixerMatrix) {
    EXPECT_TRUE(ValidateAudioSemanticMatrix(ValidMatrix()).has_value());
}

TEST(AudioSemanticMatrixTests, RejectsInvalidOrAmbiguousAxes) {
    auto snapshot = ValidMatrix();
    snapshot.inputs[1].portId = snapshot.inputs[0].portId;
    auto result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticMatrixValidationError::DuplicateInputPort);

    snapshot = ValidMatrix();
    snapshot.outputs[0].signalKind = AudioSemanticSignalKind::None;
    result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticMatrixValidationError::InvalidOutput);

    snapshot = ValidMatrix();
    snapshot.outputs[0].outputRole = AudioSemanticMatrixOutputRole::None;
    result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticMatrixValidationError::InvalidOutput);

    snapshot = ValidMatrix();
    snapshot.coefficientMaximum = 100;
    snapshot.coefficients[0] = 101;
    result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticMatrixValidationError::CoefficientOutOfRange);
}

TEST(AudioSemanticMatrixTests, SPro24MapsRouterSourcesWithoutLeakingBlockIds) {
    DICE::DiceMixerCoefficients coefficients{};
    coefficients.inputCount = 18;
    coefficients.outputCount = 16;
    coefficients.values[0] = 0x1234;
    coefficients.values[17] = 0x2345;

    DICE::DiceRouterEntries routes{};
    routes.count = 2;
    routes.entries[0] = {
        .destinationBlock = 2, .destinationChannel = 0,
        .sourceBlock = 11, .sourceChannel = 3,
    };
    routes.entries[1] = {
        .destinationBlock = 3, .destinationChannel = 1,
        .sourceBlock = 4, .sourceChannel = 1,
    };

    AudioSemanticMatrixSnapshot snapshot{};
    ASSERT_TRUE(DICE::Focusrite::BuildSPro24DspSemanticMatrix(coefficients, routes, snapshot));
    EXPECT_EQ(snapshot.deviceKind, DICE::Focusrite::kSPro24DspSemanticDeviceKind);
    EXPECT_EQ(snapshot.inputCount, 18);
    EXPECT_EQ(snapshot.outputCount, 16);
    EXPECT_EQ(snapshot.inputs[0].signalKind, AudioSemanticSignalKind::HostStream);
    EXPECT_EQ(snapshot.inputs[0].signalIndex, 4);
    EXPECT_EQ(snapshot.inputs[17].signalKind, AudioSemanticSignalKind::AnalogLine);
    EXPECT_EQ(snapshot.inputs[17].signalIndex, 2);
    EXPECT_EQ(snapshot.Coefficient(0, 0), 0x1234);
    EXPECT_EQ(snapshot.Coefficient(0, 17), 0x2345);
    EXPECT_EQ(snapshot.outputs[0].outputRole, AudioSemanticMatrixOutputRole::MonitorMix);
    EXPECT_EQ(snapshot.outputs[0].channelRole, AudioSemanticMatrixChannelRole::Left);
    EXPECT_EQ(snapshot.outputs[1].channelRole, AudioSemanticMatrixChannelRole::Right);
    EXPECT_EQ(snapshot.outputs[0].presentationGroupId,
              snapshot.outputs[1].presentationGroupId);
    EXPECT_EQ(snapshot.outputs[8].outputRole, AudioSemanticMatrixOutputRole::MonitorMix);
    EXPECT_EQ(snapshot.outputs[8].channelRole, AudioSemanticMatrixChannelRole::Left);
    EXPECT_EQ(snapshot.outputs[9].channelRole, AudioSemanticMatrixChannelRole::Right);
    EXPECT_TRUE(ValidateAudioSemanticMatrix(snapshot).has_value());
}

TEST(AudioSemanticMatrixTests, SPro24MapsTheCapturedCurrentConfigRouter) {
    // 2026-08-24 read-only capture from the Saffire Pro 24 DSP's active
    // CURRENT_CONFIG low-rate router at 0xFFFFE0200D24.  This is intentionally
    // not the empty staging router at 0xFFFFE02006E8.
    constexpr std::array<uint32_t, 49> kCapturedRouterWords = {
        0x00000030, 0x00004248, 0x00004349, 0x000040b2, 0x000041b3, 0x000006b4,
        0x000007b5, 0x000010b6, 0x000011b7, 0x000012b8, 0x000013b9, 0x000014ba,
        0x000015bb, 0x000016bc, 0x000017bd, 0x0000b040, 0x0000b141, 0x0000b042,
        0x0000b143, 0x0000b044, 0x0000b145, 0x00002006, 0x00002107, 0x000042be,
        0x000043bf, 0x00004820, 0x00004921, 0x00004022, 0x00004123, 0x00001024,
        0x00001125, 0x00001226, 0x00001327, 0x00001428, 0x00001529, 0x0000162a,
        0x0000172b, 0x0000062c, 0x0000072d, 0x0000b02e, 0x0000b12f, 0x00004e30,
        0x00004f31, 0x000048b0, 0x000049b1, 0x0000284e, 0x0000294f, 0x000020f0,
        0x000021f0,
    };
    std::array<uint8_t, kCapturedRouterWords.size() * sizeof(uint32_t)> wire{};
    for (size_t index = 0; index < kCapturedRouterWords.size(); ++index) {
        const uint32_t word = kCapturedRouterWords[index];
        wire[index * 4] = static_cast<uint8_t>(word >> 24U);
        wire[index * 4 + 1U] = static_cast<uint8_t>(word >> 16U);
        wire[index * 4 + 2U] = static_cast<uint8_t>(word >> 8U);
        wire[index * 4 + 3U] = static_cast<uint8_t>(word);
    }

    DICE::DiceRouterEntries routes{};
    ASSERT_TRUE(DICE::DecodeDiceRouterEntries(
        std::span<const uint8_t>{wire}.subspan(sizeof(uint32_t)), 48, routes));

    DICE::DiceMixerCoefficients coefficients{};
    coefficients.inputCount = 18;
    coefficients.outputCount = 16;

    AudioSemanticMatrixSnapshot snapshot{};
    ASSERT_TRUE(DICE::Focusrite::BuildSPro24DspSemanticMatrix(coefficients, routes, snapshot));
    const auto expectInput = [&snapshot](uint32_t index, uint32_t portId,
                                         AudioSemanticSignalKind kind, uint32_t signalIndex) {
        EXPECT_EQ(snapshot.inputs[index].portId, portId);
        EXPECT_EQ(snapshot.inputs[index].signalKind, kind);
        EXPECT_EQ(snapshot.inputs[index].signalIndex, signalIndex);
    };
    expectInput(0, 0x5352'0001, AudioSemanticSignalKind::Auxiliary, 1);
    expectInput(4, 0x5352'0005, AudioSemanticSignalKind::DigitalAdat, 1);
    expectInput(12, 0x5352'000D, AudioSemanticSignalKind::DigitalSpdif, 1);
    expectInput(14, 0x5352'000F, AudioSemanticSignalKind::HostStream, 1);
    expectInput(17, 0x5352'0012, AudioSemanticSignalKind::Auxiliary, 4);
    EXPECT_EQ(snapshot.inputs[14].channelRole, AudioSemanticMatrixChannelRole::Left);
    EXPECT_EQ(snapshot.inputs[15].channelRole, AudioSemanticMatrixChannelRole::Right);
    EXPECT_EQ(snapshot.inputs[14].presentationGroupId,
              snapshot.inputs[15].presentationGroupId);
    EXPECT_EQ(snapshot.inputs[4].channelRole, AudioSemanticMatrixChannelRole::Mono);
    EXPECT_NE(snapshot.inputs[4].presentationGroupId,
              snapshot.inputs[5].presentationGroupId);
    EXPECT_EQ(snapshot.gainLaw, AudioSemanticMatrixGainLaw::UnsignedQ214Amplitude);
    ASSERT_EQ(snapshot.outputCount, 16U);
    EXPECT_EQ(snapshot.outputs[0].outputRole, AudioSemanticMatrixOutputRole::MonitorMix);
    EXPECT_EQ(snapshot.outputs[8].outputRole, AudioSemanticMatrixOutputRole::MonitorMix);
    EXPECT_EQ(snapshot.outputs[9].outputRole, AudioSemanticMatrixOutputRole::MonitorMix);
}

TEST(AudioSemanticMatrixTests, SPro24StereoStripUsesPairedNativeCellsAndHardPanMute) {
    DICE::DiceMixerCoefficients coefficients{};
    coefficients.inputCount = 18;
    coefficients.outputCount = 16;
    DICE::DiceRouterEntries routes{};
    // Active-router slots 14/15 are the host playback stereo pair on SPro24.
    routes.count = 2;
    routes.entries[0] = {.destinationBlock = 2, .destinationChannel = 14,
                         .sourceBlock = 11, .sourceChannel = 0};
    routes.entries[1] = {.destinationBlock = 2, .destinationChannel = 15,
                         .sourceBlock = 11, .sourceChannel = 1};

    AudioSemanticMatrixSnapshot snapshot{};
    ASSERT_TRUE(DICE::Focusrite::BuildSPro24DspSemanticMatrix(coefficients, routes, snapshot));

    const auto layout = DICE::Focusrite::ResolveSPro24DspStereoStrip(
        coefficients, routes,
        snapshot.outputs[0].presentationGroupId,
        snapshot.inputs[14].presentationGroupId);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->inputLeft, 14U);
    EXPECT_EQ(layout->inputRight, 15U);
    EXPECT_EQ(layout->outputLeft, 0U);
    EXPECT_EQ(layout->outputRight, 1U);

    const auto centered = DICE::Focusrite::MakeSPro24DspStereoStripCoefficients(0, 0);
    ASSERT_TRUE(centered.has_value());
    EXPECT_EQ(centered->left, centered->right);
    EXPECT_GT(centered->left, 0U);
    EXPECT_LE(centered->left, 0x4000U);

    const auto hardLeft = DICE::Focusrite::MakeSPro24DspStereoStripCoefficients(0, -1000);
    ASSERT_TRUE(hardLeft.has_value());
    EXPECT_EQ(hardLeft->left, 0x4000U);
    EXPECT_EQ(hardLeft->right, 0U);

    const auto hardRight = DICE::Focusrite::MakeSPro24DspStereoStripCoefficients(0, 1000);
    ASSERT_TRUE(hardRight.has_value());
    EXPECT_EQ(hardRight->left, 0U);
    EXPECT_EQ(hardRight->right, 0x4000U);

    EXPECT_FALSE(DICE::Focusrite::MakeSPro24DspStereoStripCoefficients(-85001, 0));
    EXPECT_FALSE(DICE::Focusrite::MakeSPro24DspStereoStripCoefficients(0, 1001));
}

} // namespace
} // namespace ASFW::Audio
