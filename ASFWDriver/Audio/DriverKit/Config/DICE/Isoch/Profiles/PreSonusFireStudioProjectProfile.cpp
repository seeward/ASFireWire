// SPDX-License-Identifier: Apache-2.0
#include "PreSonusFireStudioProjectProfile.hpp"

#include "../../../../../../DeviceProfiles/Audio/AudioDeviceIds.hpp"

namespace ASFW::Isoch::Audio::DICE::Profiles {
namespace {

void FillStreamConfig(DiceStreamConfig& out, DiceStreamDirection direction) noexcept {
    using Profile = PreSonusFireStudioProjectProfile;
    out = DiceStreamConfig{};
    out.direction = direction;
    out.sampleRate = Profile::kSampleRateHz;
    out.pcmChannels = Profile::kPcmChannels;
    out.midiSlots = Profile::kMidiSlots;
    out.dbs = Profile::kDbs;
    out.streamMode = Encoding::StreamMode::kBlocking;
    out.framesPerDataPacket = 8;
    out.fdf = 0x02;
    out.fmt = 0x10;
}

} // namespace

const char* PreSonusFireStudioProjectProfile::Name() const noexcept {
    return "PreSonus FireStudio Project (DICE)";
}

bool PreSonusFireStudioProjectProfile::Matches(const DiceDeviceIdentity& identity) const noexcept {
    using namespace ASFW::DeviceProfiles::Audio;
    return identity.vendorId == kPreSonusVendorId && identity.modelId == kFireStudioProjectModelId;
}

DiceDeviceQuirks PreSonusFireStudioProjectProfile::Quirks() const noexcept {
    // Project uses generic DICE in Linux (dice-stream.c -> amdtp-am824.c)
    // and FFADO 2.5.0 (dice_avdevice.cpp -> AmdtpTransmitStreamProcessor.cpp).
    // Both send labelled AM824 PCM and empty MIDI, with header-only NO-DATA.
    // Do not inherit the raw-PCM/Saffire policy from the StudioLive profile.
    // This is a source-backed first-test format, not a Project packet capture.
    return DiceDeviceQuirks{};
}

std::vector<uint32_t> PreSonusFireStudioProjectProfile::SupportedSampleRates() const {
    // Both rates use the captured low-rate 10 PCM + 1 MIDI geometry. Linux
    // dice-stream.c:19-30 groups 44.1/48 kHz in mode 0; the existing generic
    // blocking AM824 path derives cadence and FDF from the selected rate.
    return {kSupportedSampleRatesHz.begin(), kSupportedSampleRatesHz.end()};
}

bool PreSonusFireStudioProjectProfile::BuildDefaultTxStreamConfig(DiceStreamConfig& out) const noexcept {
    FillStreamConfig(out, DiceStreamDirection::HostToDevice);
    return true;
}

bool PreSonusFireStudioProjectProfile::BuildDefaultRxStreamConfig(DiceStreamConfig& out) const noexcept {
    FillStreamConfig(out, DiceStreamDirection::DeviceToHost);
    return true;
}

} // namespace ASFW::Isoch::Audio::DICE::Profiles
