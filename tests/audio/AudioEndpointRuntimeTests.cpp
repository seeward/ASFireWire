#include <gtest/gtest.h>

#include "Testing/HostDriverKitStubs.hpp"
#include "Audio/Core/AudioEndpointRuntime.hpp"
#include "Audio/Config/AudioConstants.hpp"

namespace {

ASFW::Audio::Devices::ResolvedAudioEndpointProfile MakeProfile() {
    ASFW::Audio::Devices::ResolvedAudioEndpointProfile profile{};
    profile.endpointId = ASFW::Audio::Devices::AudioEndpointId{41};
    profile.deviceInstanceId = ASFW::Discovery::DeviceInstanceId{17};
    profile.observedGuid = 0x1020304050607080ULL;
    profile.currentSampleRateHz = 48000;
    profile.runtimeCaps.sampleRateHz = 48000;
    profile.runtimeCaps.hostInputPcmChannels = 6;
    profile.runtimeCaps.hostOutputPcmChannels = 4;
    profile.runtimeCaps.deviceToHostAm824Slots = 6;
    profile.runtimeCaps.hostToDeviceAm824Slots = 4;
    profile.runtimeCaps.deviceToHostStreamCount = 1;
    profile.runtimeCaps.hostToDeviceStreamCount = 1;
    profile.runtimeCaps.deviceToHostStreams[0] = {.pcmChannels = 6,
                                                  .am824Slots = 6};
    profile.runtimeCaps.hostToDeviceStreams[0] = {.pcmChannels = 4,
                                                  .am824Slots = 4};
    return profile;
}

} // namespace

TEST(AudioEndpointRuntime, ResolvedEndpointDoesNotPublishBindingBeforeMemoryCopy) {
    ASFW::Audio::AudioEndpointRuntime runtime(MakeProfile());

    ASFW::Audio::Runtime::DirectAudioBindingSnapshot snapshot{};
    EXPECT_FALSE(runtime.CopyDirectAudioBinding(snapshot));
    EXPECT_FALSE(snapshot.valid);

    ASFW::Audio::Runtime::AudioTelemetryEndpointSnapshot telemetry{};
    ASSERT_TRUE(runtime.CopyAudioTelemetrySnapshot(telemetry));
    EXPECT_EQ(telemetry.endpointId, 41U);
    EXPECT_EQ(telemetry.deviceInstanceId, 17U);
    EXPECT_EQ(telemetry.observedGuid, 0x1020304050607080ULL);
    EXPECT_EQ(telemetry.flags &
                  ASFW::Audio::Runtime::kAudioTelemetryBindingReady,
              0U);
}

TEST(AudioEndpointRuntime, TelemetryKeepsConfiguredUnboundEndpointVisible) {
    ASFW::Audio::AudioEndpointRuntime runtime(MakeProfile());

    ASFW::Audio::Runtime::AudioTelemetryEndpointSnapshot telemetry{};
    ASSERT_TRUE(runtime.CopyAudioTelemetrySnapshot(telemetry));
    EXPECT_EQ(telemetry.observedGuid, 0x1020304050607080ULL);
    EXPECT_EQ(telemetry.sampleRateHz, 48'000U);
    EXPECT_EQ(telemetry.outputChannels, 4U);
    EXPECT_EQ(telemetry.inputChannels, 6U);
    EXPECT_EQ(telemetry.flags &
                  ASFW::Audio::Runtime::kAudioTelemetryBindingReady,
              0U);
}

TEST(AudioEndpointRuntime, BadCopyArgsZeroOutputs) {
    ASFW::Audio::AudioEndpointRuntime runtime(MakeProfile());

    IOMemoryDescriptor* inputMemory = reinterpret_cast<IOMemoryDescriptor*>(0x1);
    IOMemoryDescriptor* controlMemory = reinterpret_cast<IOMemoryDescriptor*>(0x2);
    uint32_t outputFrames = 99;
    uint32_t outputChannels = 98;
    uint32_t inputFrames = 97;
    uint32_t inputChannels = 96;
    uint32_t sampleRateHz = 95;
    uint64_t generation = 94;

    const kern_return_t kr = runtime.CopyDirectAudioMemory(nullptr,
                                                           &inputMemory,
                                                           &controlMemory,
                                                           &outputFrames,
                                                           &outputChannels,
                                                           &inputFrames,
                                                           &inputChannels,
                                                           &sampleRateHz,
                                                           &generation);

    EXPECT_EQ(kr, kIOReturnBadArgument);
    EXPECT_EQ(inputMemory, nullptr);
    EXPECT_EQ(controlMemory, nullptr);
    EXPECT_EQ(outputFrames, 0u);
    EXPECT_EQ(outputChannels, 0u);
    EXPECT_EQ(inputFrames, 0u);
    EXPECT_EQ(inputChannels, 0u);
    EXPECT_EQ(sampleRateHz, 0u);
    EXPECT_EQ(generation, 0u);
}

TEST(AudioEndpointRuntime, CopyDirectAudioMemoryAllocatesCompleteDuplexBinding) {
    ASFW::Audio::AudioEndpointRuntime runtime(MakeProfile());

    IOMemoryDescriptor* outputMemory = nullptr;
    IOMemoryDescriptor* inputMemory = nullptr;
    IOMemoryDescriptor* controlMemory = nullptr;
    uint32_t outputFrames = 0;
    uint32_t outputChannels = 0;
    uint32_t inputFrames = 0;
    uint32_t inputChannels = 0;
    uint32_t sampleRateHz = 0;
    uint64_t generation = 0;

    const kern_return_t kr = runtime.CopyDirectAudioMemory(&outputMemory,
                                                           &inputMemory,
                                                           &controlMemory,
                                                           &outputFrames,
                                                           &outputChannels,
                                                           &inputFrames,
                                                           &inputChannels,
                                                           &sampleRateHz,
                                                           &generation);

    ASSERT_EQ(kr, kIOReturnSuccess);
    ASSERT_NE(outputMemory, nullptr);
    ASSERT_NE(inputMemory, nullptr);
    ASSERT_NE(controlMemory, nullptr);
    EXPECT_EQ(outputFrames, ASFW::Audio::Config::kAudioOutputRingFrames);
    EXPECT_EQ(inputFrames, ASFW::Audio::Config::kAudioRingBufferFrames);
    EXPECT_EQ(outputChannels, 4u);
    EXPECT_EQ(inputChannels, 6u);
    EXPECT_EQ(sampleRateHz, 48000u);
    EXPECT_GT(generation, 0u);

    ASFW::Audio::Runtime::DirectAudioBindingSnapshot snapshot{};
    ASSERT_TRUE(runtime.CopyDirectAudioBinding(snapshot));
    EXPECT_TRUE(snapshot.IsValidDuplex());
    EXPECT_EQ(snapshot.outputFrames, outputFrames);
    EXPECT_EQ(snapshot.outputChannels, outputChannels);
    EXPECT_EQ(snapshot.inputFrames, inputFrames);
    EXPECT_EQ(snapshot.inputChannels, inputChannels);
    EXPECT_EQ(snapshot.sampleRateHz, sampleRateHz);
    EXPECT_EQ(snapshot.generation, generation);

    outputMemory->release();
    inputMemory->release();
    controlMemory->release();
}

TEST(AudioEndpointRuntime, PlaybackOnlyPresentationKeepsPhysicalReturnRingForDuplexTransport) {
    auto profile = MakeProfile();
    profile.runtimeCaps.hostInputPcmChannels = 0; // CoreAudio-visible topology.
    profile.runtimeCaps.hostOutputPcmChannels = 2;
    profile.runtimeCaps.deviceToHostAm824Slots = 2;
    profile.runtimeCaps.hostToDeviceAm824Slots = 2;
    profile.runtimeCaps.deviceToHostStreams[0] = {.pcmChannels = 2,
                                                  .am824Slots = 2};
    profile.runtimeCaps.hostToDeviceStreams[0] = {.pcmChannels = 2,
                                                  .am824Slots = 2};
    ASFW::Audio::AudioEndpointRuntime runtime(profile);

    IOMemoryDescriptor* outputMemory = nullptr;
    IOMemoryDescriptor* inputMemory = nullptr;
    IOMemoryDescriptor* controlMemory = nullptr;
    uint32_t outputFrames = 0;
    uint32_t outputChannels = 0;
    uint32_t inputFrames = 0;
    uint32_t inputChannels = 0;
    uint32_t sampleRateHz = 0;
    uint64_t generation = 0;

    ASSERT_EQ(runtime.CopyDirectAudioMemory(&outputMemory,
                                            &inputMemory,
                                            &controlMemory,
                                            &outputFrames,
                                            &outputChannels,
                                            &inputFrames,
                                            &inputChannels,
                                            &sampleRateHz,
                                            &generation),
              kIOReturnSuccess);
    EXPECT_EQ(outputChannels, 2U);
    EXPECT_EQ(inputChannels, 2U);

    outputMemory->release();
    inputMemory->release();
    controlMemory->release();
}

TEST(AudioEndpointRuntime, ConfigurationChangesReuseMaximumCapacityDescriptors) {
    auto profile = MakeProfile();
    profile.configurationCapabilityCount = 1;
    auto& adat = profile.configurationCapabilities[0];
    adat.configuration = {
        .sampleRate = 44100,
        .opticalInput = ASFW::Configuration::OpticalMode::Adat,
        .opticalOutput = ASFW::Configuration::OpticalMode::Adat,
    };
    adat.runtimeCaps = profile.runtimeCaps;
    adat.runtimeCaps.sampleRateHz = 44100;
    adat.runtimeCaps.hostInputPcmChannels = 16;
    adat.runtimeCaps.hostOutputPcmChannels = 12;
    adat.runtimeCaps.deviceToHostAm824Slots = 17;
    adat.runtimeCaps.hostToDeviceAm824Slots = 13;
    adat.runtimeCaps.deviceToHostStreams[0] = {.pcmChannels = 16, .am824Slots = 17};
    adat.runtimeCaps.hostToDeviceStreams[0] = {.pcmChannels = 12, .am824Slots = 13};
    ASFW::Audio::AudioEndpointRuntime runtime(profile);

    IOMemoryDescriptor* firstOutput = nullptr;
    IOMemoryDescriptor* firstInput = nullptr;
    IOMemoryDescriptor* firstControl = nullptr;
    uint32_t outputFrames = 0;
    uint32_t outputChannels = 0;
    uint32_t inputFrames = 0;
    uint32_t inputChannels = 0;
    uint32_t rate = 0;
    uint64_t firstGeneration = 0;
    ASSERT_EQ(runtime.CopyDirectAudioMemory(
                  &firstOutput, &firstInput, &firstControl, &outputFrames,
                  &outputChannels, &inputFrames, &inputChannels, &rate,
                  &firstGeneration),
              kIOReturnSuccess);
    EXPECT_EQ(outputChannels, 4U);
    EXPECT_EQ(inputChannels, 6U);

    ASSERT_TRUE(runtime.ApplyConfiguration(adat.runtimeCaps));

    IOMemoryDescriptor* secondOutput = nullptr;
    IOMemoryDescriptor* secondInput = nullptr;
    IOMemoryDescriptor* secondControl = nullptr;
    uint64_t secondGeneration = 0;
    ASSERT_EQ(runtime.CopyDirectAudioMemory(
                  &secondOutput, &secondInput, &secondControl, &outputFrames,
                  &outputChannels, &inputFrames, &inputChannels, &rate,
                  &secondGeneration),
              kIOReturnSuccess);
    EXPECT_EQ(secondOutput, firstOutput);
    EXPECT_EQ(secondInput, firstInput);
    EXPECT_EQ(secondControl, firstControl);
    EXPECT_EQ(outputChannels, 12U);
    EXPECT_EQ(inputChannels, 16U);
    EXPECT_EQ(rate, 44100U);
    EXPECT_GT(secondGeneration, firstGeneration);

    firstOutput->release();
    firstInput->release();
    firstControl->release();
    secondOutput->release();
    secondInput->release();
    secondControl->release();
}
