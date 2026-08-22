// SPDX-License-Identifier: Apache-2.0

#include "BeBoBProfileBuilder.hpp"
#include "../Common/CommonProfileBuilder.hpp"
#include "MAudio/MAudioCaptureChannelMap.hpp"
#include "MAudio/MAudioDuplexPolicy.hpp"
#include "MAudio/MAudioSpecialTiming.hpp"
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

void AddSpecialConfigurationCapability(
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
    capability.captureChannelMap = MAudio::CaptureChannelMapFor(
        profile.profileBuilder, formation->capturePcmChannels);
}

void AddSpecialConfigurationCapabilities(
    Devices::ResolvedAudioEndpointProfile& profile) noexcept {
    constexpr std::array<uint32_t, 2> kRates{44100, 48000};
    constexpr std::array<::ASFW::Audio::BeBoB::MAudioDigitalFormat, 2> kFormats{
        ::ASFW::Audio::BeBoB::MAudioDigitalFormat::SPDIF,
        ::ASFW::Audio::BeBoB::MAudioDigitalFormat::ADAT,
    };
    for (const uint32_t rateHz : kRates) {
        for (const auto captureFormat : kFormats) {
            for (const auto playbackFormat : kFormats) {
                AddSpecialConfigurationCapability(profile, rateHz, captureFormat,
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
    // These two personas do not report their capture channel order and cannot
    // be asked for it — the BridgeCo channel-position extension their firmware
    // would have to answer is the same one that freezes them. The order comes
    // from M-Audio's own driver instead; see MAudioCaptureChannelMap.hpp for the
    // provenance and for why only the 1814 carries the input skew.
    profile.captureChannelMap = MAudio::CaptureChannelMapFor(
        context.staticPlan.profileBuilder, profile.runtimeCaps.hostInputPcmChannels);


    if (MAudio::UsesSpecialDuplexPolicy(context.staticPlan.profileBuilder)) {
        // Both special personas deliberately expose only the two base-rate
        // formations for now. The firmware supports more rate bands, but this
        // coordinator/backend does not yet carry their geometry through the
        // complete control path. Capture and playback remain independent: that
        // is the device's dig_in_fmt/dig_out_fmt contract
        // (bebob_maudio.c:166-216, 230-253).
        //
        // The formation table is shared — only the rate *count* differs between
        // the two (ProjectMix stops after 96 kHz), so the 44.1/48 geometry these
        // capabilities describe is identical. ProjectMix's own
        // `SwitchDigitalSignals` calls the 1814's and then repeats one step, so
        // the switching behaviour is shared too.
        AddSpecialConfigurationCapabilities(profile);
    }
    // MIDI is multiplexed into the single AM824 conformant-data slot both
    // personas already carry — the port count does NOT change DBS. Linux sets
    // `.midi = 1` unconditionally for both (bebob_maudio.c:249,252) and the
    // AM824 layer caps conformant-data channels at one
    // (`AM824_MAX_CHANNELS_FOR_MIDI`), muxing up to eight ports through it.
    // Only the number of ports presented differs.
    if (const uint16_t midiPorts =
            MAudio::SpecialMidiPortCount(context.staticPlan.profileBuilder);
        midiPorts != 0) {
        profile.runtimeCaps.deviceToHostStreams[0].midiPorts = midiPorts;
        profile.runtimeCaps.hostToDeviceStreams[0].midiPorts = midiPorts;
        for (uint8_t i = 0; i < profile.configurationCapabilityCount; ++i) {
            auto& caps = profile.configurationCapabilities[i].runtimeCaps;
            caps.deviceToHostStreams[0].midiPorts = midiPorts;
            caps.hostToDeviceStreams[0].midiPorts = midiPorts;
        }
    }
    profile.facets.push_back({Devices::FacetKind::Clock, 1});
    Common::AddDefaultTiming(profile, 4000);
    for (uint8_t i = 0; i < profile.timingCount; ++i) {
        // The special-firmware personas publish their own per-rate figures, and
        // the vendor keeps reported latency and safety offset as two separate
        // device methods for a reason — see MAudioSpecialTiming.hpp. Everything
        // else keeps the family default.
        MAudio::SpecialRateTiming special{};
        if (MAudio::SpecialRateTimingFor(context.staticPlan.profileBuilder,
                                         profile.timing[i].sampleRateHz, special)) {
            profile.timing[i].inputLatencyFrames = special.inputLatencyFrames;
            profile.timing[i].outputLatencyFrames = special.outputLatencyFrames;
            profile.timing[i].inputSafetyFrames = special.safetyOffsetFrames;
            profile.timing[i].outputSafetyFrames = special.safetyOffsetFrames;
            continue;
        }
        profile.timing[i].inputLatencyFrames = 128;
        profile.timing[i].outputLatencyFrames = 128;
        profile.timing[i].inputSafetyFrames = 64;
        profile.timing[i].outputSafetyFrames = 64;
    }
    return result;
}

} // namespace ASFW::Audio::Families::BeBoB
