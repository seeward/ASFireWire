// SPDX-License-Identifier: Apache-2.0

#include "BeBoBProfileBuilder.hpp"
#include "../Common/CommonProfileBuilder.hpp"

namespace ASFW::Audio::Families::BeBoB {

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
