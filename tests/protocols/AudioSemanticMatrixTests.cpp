// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

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
    snapshot.inputs[0] = {101, AudioSemanticSignalKind::AnalogLine, 1};
    snapshot.inputs[1] = {102, AudioSemanticSignalKind::HostStream, 1};
    snapshot.outputs[0] = {201, AudioSemanticSignalKind::Headphone, 1};
    snapshot.outputs[1] = {202, AudioSemanticSignalKind::AnalogLine, 1};
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
    EXPECT_TRUE(ValidateAudioSemanticMatrix(snapshot).has_value());
}

} // namespace
} // namespace ASFW::Audio
