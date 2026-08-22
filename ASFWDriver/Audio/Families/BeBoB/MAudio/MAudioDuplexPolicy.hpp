// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// Shared profile predicate for the M-Audio 1814 / ProjectMix firmware path.
// Keep this at the family boundary so transport, AudioDriverKit, and the
// duplex coordinator make the same narrow opt-in decision.

#pragma once

#include <cstdint>

#include "../../../../DeviceProfiles/Audio/AudioDeviceCatalog.hpp"

namespace ASFW::Audio::Families::BeBoB::MAudio {

[[nodiscard]] constexpr bool UsesSpecialDuplexPolicy(
    DeviceProfiles::Audio::ProfileBuilderId profileBuilder) noexcept {
    using ProfileBuilderId = DeviceProfiles::Audio::ProfileBuilderId;
    return profileBuilder == ProfileBuilderId::MAudioFireWire1814 ||
           profileBuilder == ProfileBuilderId::MAudioProjectMix;
}

/// MIDI ports presented by each special-firmware persona.
///
/// This does **not** change the wire geometry. Both personas carry exactly one
/// AM824 conformant-data slot per direction — Linux sets `.midi = 1`
/// unconditionally for both (`bebob_maudio.c:249,252`) and the AM824 layer caps
/// conformant-data channels at one (`AM824_MAX_CHANNELS_FOR_MIDI`), muxing up to
/// eight ports through that single slot. DBS is therefore identical; only the
/// number of ports presented to the OS differs, exactly as the vendor's
/// `SetupAudioEngine` registers 1 for the 1814 and 2 ("Control", "External")
/// for the ProjectMix.
///
/// Returns 0 for anything that is not one of the two personas.
[[nodiscard]] constexpr uint16_t SpecialMidiPortCount(
    DeviceProfiles::Audio::ProfileBuilderId profileBuilder) noexcept {
    using ProfileBuilderId = DeviceProfiles::Audio::ProfileBuilderId;
    switch (profileBuilder) {
        case ProfileBuilderId::MAudioFireWire1814: return 1;
        case ProfileBuilderId::MAudioProjectMix:   return 2;
        default:                                   return 0;
    }
}

} // namespace ASFW::Audio::Families::BeBoB::MAudio
