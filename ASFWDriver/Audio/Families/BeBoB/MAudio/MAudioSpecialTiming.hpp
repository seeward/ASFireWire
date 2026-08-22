// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialTiming.hpp — the reported latency and safety offset M-Audio's own
// driver publishes for the FireWire 1814 and ProjectMix I/O.
//
// The vendor keeps these as two *separate* device methods, and conflating them
// is the classic mistake:
//
//   `GetRoundTripLatencyForFDF`  (per model)  -> reported presentation latency
//   `GetSafetyOffsetForFDF`      (shared base) -> real timing headroom
//
// `m_audio_b_FWBaseEngine::ResetLatency` @ 0x3f7a consumes both and shows
// exactly how the round-trip figure is split:
//
//     v3 = device->GetRoundTripLatencyForFDF(fdf);
//     engine->vtable[2928](this, (v3 + 1) >> 1);   // input  reported latency
//     v5 = device->GetRoundTripLatencyForFDF(fdf);
//     engine->vtable[2920](this, v5 >> 1);         // output reported latency
//     v6 = device->GetSafetyOffsetForFDF(fdf);
//     engine->vtable[2936](this, v6);              // safety offset
//
// One half rounded up and one rounded down, so the two sum back to the
// round-trip figure exactly. Those two vtable slots match what we independently
// recovered from Saffire.kext (input 2928 / output 2920) — same IOAudioEngine
// base, so the offsets hold across vendors.
//
// Reported latency is sync metadata: CoreAudio surfaces it so hosts can do
// plugin delay compensation and align recorded tracks. It never enters the
// timestamps and cannot cause a dropout — but getting it wrong lands recordings
// at the wrong offset. The safety offset is the opposite: pure timing headroom,
// where a value that is too small produces glitches.
//
// Sources: `com_m_audio_FW1814Device::GetRoundTripLatencyForFDF`      @ 0xcf30
//          `com_m_audio_FWProjectMixDevice::GetRoundTripLatencyForFDF` @ 0x1a5b0
//          `m_audio_b_FWAudioDevice::GetSafetyOffsetForFDF`            @ 0x1d58

#pragma once

#include "../../../../DeviceProfiles/Audio/AudioDeviceCatalog.hpp"

#include <cstdint>

namespace ASFW::Audio::Families::BeBoB::MAudio {

struct SpecialRateTiming final {
    uint32_t inputLatencyFrames{0};
    uint32_t outputLatencyFrames{0};
    uint32_t safetyOffsetFrames{0};
};

/// Round-trip latency in frames. The vendor switches on FDF; the mapping to a
/// rate is the IEC 61883-6 sampling-frequency code (2 = 48 kHz, 3 = 88.2 kHz,
/// 4 = 96 kHz, 5 = 176.4 kHz, 6 = 192 kHz), with everything else — including
/// 44.1 kHz — taking the `default:` arm.
[[nodiscard]] constexpr uint32_t FireWire1814RoundTripLatencyFrames(
    uint32_t rateHz) noexcept {
    switch (rateHz) {
        case 48000:  return 225;
        case 88200:  return 358;
        case 96000:  return 378;
        case 176400: return 642;
        case 192000: return 698;
        default:     return 210;  // 44.1 kHz and below
    }
}

/// ProjectMix has no FDF 5/6 arms at all — they fall to its default — which is
/// the rate truncation expressed a second time in the binary, independently of
/// the shorter rate array its `GetDeviceChannelsForSampleRate` installs.
[[nodiscard]] constexpr uint32_t ProjectMixRoundTripLatencyFrames(
    uint32_t rateHz) noexcept {
    switch (rateHz) {
        case 48000: return 208;
        case 88200: return 342;
        case 96000: return 366;
        default:    return 196;  // 44.1 kHz; 176.4/192 are not supported
    }
}

/// Shared across both personas — it lives on `m_audio_b_FWAudioDevice`, not on
/// either device class. Every entry is one millisecond of frames at its rate,
/// which is almost certainly how the table was chosen.
[[nodiscard]] constexpr uint32_t SpecialSafetyOffsetFrames(uint32_t rateHz) noexcept {
    switch (rateHz) {
        case 48000:  return 48;
        case 88200:  return 88;
        case 96000:  return 96;
        case 176400: return 176;
        case 192000: return 192;
        default:     return 44;  // 44.1 kHz and below
    }
}

/// The vendor's timing for one rate, or `std::nullopt` for a profile that is not
/// one of the special-firmware personas.
[[nodiscard]] constexpr bool SpecialRateTimingFor(
    DeviceProfiles::Audio::ProfileBuilderId profileBuilder,
    uint32_t rateHz,
    SpecialRateTiming& out) noexcept {
    using ProfileBuilderId = DeviceProfiles::Audio::ProfileBuilderId;

    uint32_t roundTrip = 0;
    if (profileBuilder == ProfileBuilderId::MAudioFireWire1814) {
        roundTrip = FireWire1814RoundTripLatencyFrames(rateHz);
    } else if (profileBuilder == ProfileBuilderId::MAudioProjectMix) {
        roundTrip = ProjectMixRoundTripLatencyFrames(rateHz);
    } else {
        return false;
    }

    // The split the vendor engine performs: input takes the rounded-up half so
    // the two halves sum back to the round-trip figure for odd values.
    out.inputLatencyFrames = (roundTrip + 1U) / 2U;
    out.outputLatencyFrames = roundTrip / 2U;
    out.safetyOffsetFrames = SpecialSafetyOffsetFrames(rateHz);
    return true;
}

} // namespace ASFW::Audio::Families::BeBoB::MAudio
