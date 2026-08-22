// SPDX-License-Identifier: Apache-2.0

#include "DiceProfileBuilder.hpp"
#include "../Common/CommonProfileBuilder.hpp"
#include "../../../Logging/Logging.hpp"
#include "../../../DeviceProfiles/Audio/AudioDeviceIds.hpp"

namespace ASFW::Audio::Families::DICE {

std::expected<Devices::ResolvedAudioEndpointProfile, Devices::ProfileBuildError>
BuildProfile(const Devices::ProfileBuildContext& context) noexcept {
    const auto* facts = std::get_if<Devices::DiceProbeFacts>(&context.probeFacts);
    if (facts == nullptr) {
        return std::unexpected(Devices::ProfileBuildError::WrongProbeFacts);
    }
    auto result = Common::BuildBase(context, facts->streams, facts->supportedRates);
    if (!result) {
        return result;
    }
    auto& profile = *result;
    profile.streamMode = Devices::StreamModePolicy::Blocking;
    profile.captureIsoChannelPolicy = Duplex::IsoChannelPolicy::Fixed;
    profile.playbackIsoChannelPolicy = Duplex::IsoChannelPolicy::Fixed;
    profile.facets.push_back({Devices::FacetKind::Clock, 1});

    using DeviceProfiles::Audio::ProfileBuilderId;
    switch (context.staticPlan.profileBuilder) {
        case ProfileBuilderId::FocusriteSPro24Dsp:
            if (facts->streams.hostInputPcmChannels == 8 &&
                facts->streams.deviceToHostAm824Slots == 9) {
                profile.captureWireFormat = Encoding::AudioWireFormat::kRawPcm24In32;
            }
            if (facts->streams.hostOutputPcmChannels == 8 &&
                facts->streams.hostToDeviceAm824Slots == 9) {
                profile.playbackWireFormat = Encoding::AudioWireFormat::kRawPcm24In32;
            }
            profile.facets.push_back({Devices::FacetKind::Mixer, 0x53503234});
            profile.txPacketPolicy.preserveFdfInNoDataPackets = true;
            break;
        case ProfileBuilderId::WeissInt202:
        case ProfileBuilderId::WeissInt203:
            profile.startPolicy.requiresPreStreamClockLock = false;
            profile.startPolicy.startOrder = {
                Duplex::HostDirection::kTransmit,
                Duplex::HostDirection::kReceive,
            };
            break;
        // [derived, not hardware-validated] The Liquid Saffire 56 is grouped
        // with its Saffire siblings: every Focusrite DICE device we have tested
        // takes host->device PCM with no AM824 labels. If it turns out to want
        // labelled AM824, this is the line to change.
        case ProfileBuilderId::FocusriteLiquidS56:
        case ProfileBuilderId::FocusriteSPro14:
        case ProfileBuilderId::FocusriteSPro24:
        case ProfileBuilderId::AlesisMultiMix:
        case ProfileBuilderId::MidasVeniceF32:
        case ProfileBuilderId::PreSonusStudioLive1602:
            profile.playbackWireFormat = Encoding::AudioWireFormat::kRawPcm24In32;
            profile.txPacketPolicy.preserveFdfInNoDataPackets = true;
            if (context.staticPlan.profileBuilder == ProfileBuilderId::AlesisMultiMix) {
                profile.txPacketPolicy.initializeNonAudioSlots = false;
            }
            // Midas Venice F16/F24/F32: derive marketing name from measured geometry.
            // All three share the same DICE identity (product 0x001) and differ
            // only in physical channel count. Unrecognized geometry keeps the
            // catalog name.
            if (context.staticPlan.profileBuilder == ProfileBuilderId::MidasVeniceF32) {
                const char* derivedName = nullptr;
                switch (facts->streams.hostInputPcmChannels) {
                    case 16: derivedName = DeviceProfiles::Audio::kMidasVeniceF16ModelName; break;
                    case 24: derivedName = DeviceProfiles::Audio::kMidasVeniceF24ModelName; break;
                    case 32: derivedName = DeviceProfiles::Audio::kMidasVeniceF32ModelName; break;
                    default: break;
                }
                if (derivedName) {
                    profile.deviceName = derivedName;
                }
                ASFW_LOG(DICE, "DiceProfileBuilder: Midas Venice geometry %u/%u \u2192 name '%s'",
                         facts->streams.hostInputPcmChannels,
                         facts->streams.hostOutputPcmChannels,
                         profile.deviceName.c_str());
            }
            break;
        default:
            return std::unexpected(Devices::ProfileBuildError::UnsupportedBuilder);
    }
    // Carry device-reported channel names to the nub for CoreAudio.
    profile.deviceInputChannelNames = facts->inputLabels;
    profile.deviceOutputChannelNames = facts->outputLabels;
    Common::AddDefaultTiming(profile, 500);
    for (uint8_t i = 0; i < profile.timingCount; ++i) {
        const uint32_t rate = profile.timing[i].sampleRateHz;
        const uint32_t framesPerPacket = rate > 96000 ? 32U : (rate > 48000 ? 16U : 8U);
        const uint32_t addend = rate > 96000 ? 4U : (rate > 48000 ? 2U : 0U);
        profile.timing[i].inputLatencyFrames =
            rate > 96000 ? 119U : (rate > 48000 ? 59U : 29U);
        profile.timing[i].outputLatencyFrames = profile.timing[i].inputLatencyFrames;
        profile.timing[i].inputSafetyFrames = (16U + addend) * framesPerPacket;
        profile.timing[i].outputSafetyFrames = (6U + addend) * framesPerPacket;
    }
    return result;
}

} // namespace ASFW::Audio::Families::DICE
