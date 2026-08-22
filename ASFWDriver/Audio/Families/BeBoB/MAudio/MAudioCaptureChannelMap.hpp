// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioCaptureChannelMap.hpp — the capture slot order of the M-Audio "special
// firmware" devices (FireWire 1814, ProjectMix I/O), and the 1814's input skew.
//
// These devices do not report their channel order. BeBoB has a mechanism for
// exactly that — the BridgeCo channel-position extension that Linux reads via
// `map_data_channels()` — but it is skipped for precisely this quirk
// (`bebob_stream.c:416-420`), because the special firmware implements no
// BridgeCo extension at all ("Special models doesn't support any bridgeco
// extension", alsa-userspace `maudio/special.rs:100`) and freezes on the
// attempt. Linux therefore falls back to identity
// (`amdtp-am824.c:100-101`), which is why ALSA presents these devices in raw
// wire order. There is nothing to query and nothing upstream to port: the map
// below was recovered from M-Audio's own driver.
//
// Source: `M-AudioFireWireBeBoB` kext.
//   com_m_audio_FW1814Device::WorkaroundFor1814InputDelay      @ 0xcba4
//   com_m_audio_FW1814Device::ResetRearrangementProcs          @ 0xcad4
//   com_m_audio_FWProjectMixDevice::ResetRearrangementProcs    @ 0x1a61e
//   ConvertInputFrames1212CS                                   @ 0x29183
//   com_m_audio_FW1814Device::com_m_audio_FW1814Device         @ 0xf04a
//
// Do not be misled by `ConvertLLRRInputToSInt32` (@ 0x2951f): its name reads
// like the de-interleaver, but it is a generic fallback that swaps indices
// 1<->2 within each group of four, and neither device reaches it. The bespoke
// converter is installed at engine+98 because the generic map pointer in
// `DeviceChannels+0x18` is NULL for these models.

#pragma once

#include "../../../Engine/Direct/Rx/RxCaptureChannelMap.hpp"
#include "../../../../DeviceProfiles/Audio/AudioDeviceCatalog.hpp"

#include <array>
#include <cstdint>

namespace ASFW::Audio::Families::BeBoB::MAudio {

/// Slot order for the 10-channel S/PDIF geometry: 8 analog + 2 S/PDIF, with
/// MIDI in the trailing AM824 slot.
///
/// The analog block is planar, not interleaved — the device emits the left
/// member of all four analog pairs, then the right member. Jack 1 lands on slot
/// 0 and jack 2 on slot 4, which is why an identity decode puts a microphone in
/// the second jack on CoreAudio input 5.
inline constexpr std::array<uint8_t, 10> kSpecialCaptureSlots10{
    0, 4, 1, 5, 2, 6, 3, 7, // analog jacks 1..8
    8, 9,                   // S/PDIF L/R
};

/// The 16-channel ADAT geometry keeps the same analog block and appends the
/// eight ADAT slots unpermuted (`WorkaroundFor1814InputDelay` widens to
/// dst[10..15] straight from src[10..15]).
inline constexpr std::array<uint8_t, 16> kSpecialCaptureSlots16{
    0, 4, 1, 5, 2, 6, 3, 7, // analog jacks 1..8
    8, 9,                   // S/PDIF L/R
    10, 11, 12, 13, 14, 15, // ADAT 1..8 (2 carried in the S/PDIF pair's stead)
};

/// The 1814 samples its six line inputs ahead of the two microphone inputs and
/// S/PDIF. The vendor driver realigns them through a 16-bank history ring, each
/// bank holding the six skewed channels; it reads a bank before overwriting it,
/// so the value it emits was captured 16 iterations earlier. The constructor
/// fills the bank table `for (i = 0; i != 96; i += 6) *v1++ = i;` — 16 banks of
/// six — which fixes the depth exactly.
inline constexpr uint32_t kFireWire1814InputDelayFrames = 16;

/// CoreAudio channels 3..8 — the six line inputs. Channels 1..2 (the microphone
/// preamps) and both S/PDIF channels are read from the current frame.
inline constexpr uint32_t kFireWire1814DelayedChannelMask = 0b1111'1100U;

/// The capture map for one of the special-firmware personas, or the identity
/// map for anything else.
///
/// ProjectMix I/O shares the permutation exactly and has **no** input skew: its
/// `ResetRearrangementProcs` calls the 1814's and then replaces the converter
/// with `ConvertInputFrames1212CS`, which reads every channel from the current
/// frame. Applying the 1814's delay to a ProjectMix would introduce the very
/// skew it does not have, so this is gated on the model, never on the family.
[[nodiscard]] inline constexpr ::ASFW::AudioEngine::Direct::Rx::RxCaptureChannelMap
CaptureChannelMapFor(DeviceProfiles::Audio::ProfileBuilderId profileBuilder,
                     uint32_t capturePcmChannels) noexcept {
    using ProfileBuilderId = DeviceProfiles::Audio::ProfileBuilderId;
    using Map = ::ASFW::AudioEngine::Direct::Rx::RxCaptureChannelMap;

    const bool isFireWire1814 = profileBuilder == ProfileBuilderId::MAudioFireWire1814;
    const bool isProjectMix = profileBuilder == ProfileBuilderId::MAudioProjectMix;
    if (!isFireWire1814 && !isProjectMix) {
        return {};
    }

    Map map{};
    map.channelCount = capturePcmChannels;
    if (capturePcmChannels == kSpecialCaptureSlots10.size()) {
        map.slotForChannel = kSpecialCaptureSlots10;
    } else if (capturePcmChannels == kSpecialCaptureSlots16.size()) {
        map.slotForChannel = kSpecialCaptureSlots16;
    } else {
        // An unexpected geometry — including the 2-channel high-rate mode,
        // where the vendor driver installs plain stereo
        // (`ConvertInputFramesOneStereoPairSkipMIDI`) and performs no
        // permutation at all. Identity is both the honest answer and the
        // vendor's own behaviour.
        return {};
    }

    if (isFireWire1814) {
        map.delayFrames = kFireWire1814InputDelayFrames;
        map.delayedChannelMask = kFireWire1814DelayedChannelMask;
    }
    return map;
}

} // namespace ASFW::Audio::Families::BeBoB::MAudio
