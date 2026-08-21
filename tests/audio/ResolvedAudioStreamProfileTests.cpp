// SPDX-License-Identifier: Apache-2.0

#include "ASFWDriver/Audio/DriverKit/Config/ResolvedAudioStreamProfile.hpp"

#include <gtest/gtest.h>

namespace {

using ASFW::Audio::Devices::AudioEndpointId;
using ASFW::Audio::Devices::ResolvedAudioEndpointProfile;
using ASFW::Audio::DriverKit::ResolvedAudioStreamProfile;

ResolvedAudioEndpointProfile MakeProfile() {
    ResolvedAudioEndpointProfile profile{};
    profile.endpointId = AudioEndpointId{1};
    profile.deviceInstanceId = ASFW::Discovery::DeviceInstanceId{1};
    profile.supportedRates = {44100, 48000};
    profile.supportedRateCount = 2;
    profile.currentSampleRateHz = 48000;
    profile.runtimeCaps.sampleRateHz = 48000;
    profile.runtimeCaps.hostToDeviceStreamCount = 1;
    profile.runtimeCaps.deviceToHostStreamCount = 1;
    profile.runtimeCaps.hostToDeviceStreams[0] = {0, 6, 7, 1};
    profile.runtimeCaps.deviceToHostStreams[0] = {1, 10, 11, 1};
    return profile;
}

TEST(ResolvedAudioStreamProfileTests, ConfigurationProjectsPacketizerTuple) {
    ResolvedAudioStreamProfile profile;
    profile.Load(MakeProfile());

    auto caps = profile.Value().runtimeCaps;
    caps.sampleRateHz = 44100;
    caps.hostInputPcmChannels = 16;
    caps.hostOutputPcmChannels = 12;
    caps.hostToDeviceStreams[0] = {0, 12, 13, 1};
    caps.deviceToHostStreams[0] = {1, 16, 17, 1};

    ASSERT_TRUE(profile.ApplyRuntimeConfiguration(caps));

    ASFW::Isoch::Audio::AudioStreamConfig tx{};
    ASFW::Isoch::Audio::AudioStreamConfig rx{};
    ASSERT_TRUE(profile.BuildDefaultTxStreamConfig(tx));
    ASSERT_TRUE(profile.BuildDefaultRxStreamConfig(rx));
    EXPECT_EQ(tx.sampleRate, 44100U);
    EXPECT_EQ(tx.pcmChannels, 12U);
    EXPECT_EQ(tx.dbs, 13U);
    EXPECT_EQ(tx.fdf, 1U);
    EXPECT_EQ(tx.framesPerDataPacket, 8U);
    EXPECT_EQ(rx.sampleRate, 44100U);
    EXPECT_EQ(rx.pcmChannels, 16U);
    EXPECT_EQ(rx.dbs, 17U);
    EXPECT_EQ(rx.fdf, 1U);
    EXPECT_EQ(rx.framesPerDataPacket, 8U);
}

TEST(ResolvedAudioStreamProfileTests, RejectsUnknownRateWithoutMutatingProfile) {
    ResolvedAudioStreamProfile profile;
    profile.Load(MakeProfile());
    auto caps = profile.Value().runtimeCaps;
    caps.sampleRateHz = 44101;

    EXPECT_FALSE(profile.ApplyRuntimeConfiguration(caps));
    EXPECT_EQ(profile.Value().currentSampleRateHz, 48000U);
    EXPECT_EQ(profile.Value().playbackFdf, 2U);
}

} // namespace
