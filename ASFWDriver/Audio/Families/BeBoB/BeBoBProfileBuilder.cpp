// SPDX-License-Identifier: Apache-2.0

#include "BeBoBProfileBuilder.hpp"
#include "../Common/CommonProfileBuilder.hpp"
#include "../../Protocols/BeBoB/MAudioSpecialFormation.hpp"

#include <array>

namespace ASFW::Audio::Families::BeBoB {
namespace {

[[nodiscard]] constexpr Configuration::OpticalMode OpticalModeFor(
    ::ASFW::Audio::BeBoB::MAudioDigitalFormat format) noexcept {
    return format == ::ASFW::Audio::BeBoB::MAudioDigitalFormat::ADAT
        ? Configuration::OpticalMode::Adat
        : Configuration::OpticalMode::Spdif;
}

void Add1814ConfigurationCapability(
    Devices::ResolvedAudioEndpointProfile& profile,
    uint32_t rateHz,
    ::ASFW::Audio::BeBoB::MAudioDigitalFormat captureFormat,
    ::ASFW::Audio::BeBoB::MAudioDigitalFormat playbackFormat) noexcept {
    if (profile.configurationCapabilityCount >= profile.configurationCapabilities.size()) {
        return;
    }
    const auto formation = ::ASFW::Audio::BeBoB::MAudioFormationFor(
        captureFormat, playbackFormat, rateHz);
    if (!formation) {
        return;
    }

    auto& capability = profile.configurationCapabilities[
        profile.configurationCapabilityCount++];
    capability.configuration = {
        .sampleRate = rateHz,
        .opticalInput = OpticalModeFor(captureFormat),
        .opticalOutput = OpticalModeFor(playbackFormat),
    };
    auto& caps = capability.runtimeCaps;
    caps.hostInputPcmChannels = formation->capturePcmChannels;
    caps.hostOutputPcmChannels = formation->playbackPcmChannels;
    caps.deviceToHostAm824Slots = formation->capturePcmChannels +
        formation->midiDataBlocks;
    caps.hostToDeviceAm824Slots = formation->playbackPcmChannels +
        formation->midiDataBlocks;
    caps.sampleRateHz = rateHz;
    caps.deviceToHostIsoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;
    caps.hostToDeviceIsoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;
    caps.deviceToHostStreamCount = 1;
    caps.hostToDeviceStreamCount = 1;
    caps.deviceToHostStreams[0].pcmChannels =
        static_cast<uint16_t>(formation->capturePcmChannels);
    caps.deviceToHostStreams[0].am824Slots =
        static_cast<uint16_t>(caps.deviceToHostAm824Slots);
    caps.deviceToHostStreams[0].isoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;
    caps.hostToDeviceStreams[0].pcmChannels =
        static_cast<uint16_t>(formation->playbackPcmChannels);
    caps.hostToDeviceStreams[0].am824Slots =
        static_cast<uint16_t>(caps.hostToDeviceAm824Slots);
    caps.hostToDeviceStreams[0].isoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;
}

void Add1814ConfigurationCapabilities(
    Devices::ResolvedAudioEndpointProfile& profile) noexcept {
    constexpr std::array<uint32_t, 2> kRates{44100, 48000};
    constexpr std::array<::ASFW::Audio::BeBoB::MAudioDigitalFormat, 2> kFormats{
        ::ASFW::Audio::BeBoB::MAudioDigitalFormat::SPDIF,
        ::ASFW::Audio::BeBoB::MAudioDigitalFormat::ADAT,
    };
    for (const uint32_t rateHz : kRates) {
        for (const auto captureFormat : kFormats) {
            for (const auto playbackFormat : kFormats) {
                Add1814ConfigurationCapability(profile, rateHz, captureFormat,
                                                playbackFormat);
            }
        }
    }
}

} // namespace


std::expected<Devices::ResolvedAudioEndpointProfile, Devices::ProfileBuildError>
BuildProfile(const Devices::ProfileBuildContext& context) noexcept {
    const auto* facts = std::get_if<Devices::BeBoBProbeFacts>(&context.probeFacts);
    if (facts == nullptr) {
        return std::unexpected(Devices::ProfileBuildError::WrongProbeFacts);
    }
    auto result = Common::BuildBase(context, facts->streams, facts->supportedRates);
    if (!result) {
        return result;
    }
    auto& profile = *result;
    profile.streamMode = Devices::StreamModePolicy::Blocking;
    profile.captureIsoChannelPolicy = Duplex::IsoChannelPolicy::IRMSelectable;
    profile.playbackIsoChannelPolicy = Duplex::IsoChannelPolicy::IRMSelectable;
    profile.startPolicy.requiresPreStreamClockLock = false;
    profile.startPolicy.postDeviceEnableDelayMs = 0;
    profile.txPacketPolicy.emptyPacketsDuringIdle =
        context.staticPlan.profileBuilder ==
        DeviceProfiles::Audio::ProfileBuilderId::TerraTecPhase88;

    // The M-Audio "special" firmware is never sent a header-only packet by its
    // own driver: in tools/1814/12.txt, a session in which the device streams,
    // every host->device packet is full size and the cadence ones carry data
    // blocks labelled as holding no audio. Scoped to these two personas because
    // that is where the evidence is; every other BeBoB device here is driven
    // with header-only cadence packets and works.
    profile.txPacketPolicy.cadencePacketsCarryDataBlocks =
        context.staticPlan.profileBuilder ==
            DeviceProfiles::Audio::ProfileBuilderId::MAudioFireWire1814 ||
        context.staticPlan.profileBuilder ==
            DeviceProfiles::Audio::ProfileBuilderId::MAudioProjectMix;
    if (context.staticPlan.profileBuilder ==
        DeviceProfiles::Audio::ProfileBuilderId::MAudioFireWire1814) {
        // 1814 V1 deliberately exposes only the two base-rate formations. The
        // firmware supports more rate bands, but this coordinator/backend does
        // not yet carry their geometry through the complete control path.
        // Capture and playback remain independent: that is the device's
        // dig_in_fmt/dig_out_fmt contract (bebob_maudio.c:166-216, 230-253).
        Add1814ConfigurationCapabilities(profile);
    }
    profile.facets.push_back({Devices::FacetKind::Clock, 1});
    Common::AddDefaultTiming(profile, 4000);
    for (uint8_t i = 0; i < profile.timingCount; ++i) {
        profile.timing[i].inputLatencyFrames = 128;
        profile.timing[i].outputLatencyFrames = 128;
        profile.timing[i].inputSafetyFrames = 64;
        profile.timing[i].outputSafetyFrames = 64;
    }
    return result;
}

} // namespace ASFW::Audio::Families::BeBoB
