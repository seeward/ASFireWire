// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// ApogeeDuetVendorCommands.hpp - Recovered Apogee Duet vendor-command catalogue.

#pragma once

#include <cstdint>

namespace ASFW::Audio::Oxford::Apogee {

/// Vendor command codes observed in the supplied Apogee Duet daemon.
///
/// This is intentionally a catalogue, not a supported-control list. In
/// particular, diagnostic and one-shot commands below must not be emitted by
/// normal driver operation merely because they are listed here. The names keep
/// the recovered daemon semantics where they differ from the operational codec
/// names, so unresolved meanings remain visible for hardware validation.
///
/// The values belong to the Apogee Duet protocol only. They are carried in the
/// OXFW VENDOR-DEPENDENT PCM command framing implemented by
/// ApogeeVendorCodec.hpp.
enum class ApogeeDuetVendorOpcode : uint8_t {
    InputPhaseInversion = 0x00,
    InputMicLevel = 0x01,
    InputPlus4dBu = 0x02,
    InputPhantomPower = 0x03,
    OutputLineLevel = 0x04,
    InputGain = 0x05,
    ControllerInstanceCount = 0x06,
    HardwareState = 0x07,
    MicsGrouped = 0x08,
    UserMute = 0x09,

    LoopbackTest = 0x0B,
    InputHiZ = 0x0C,

    MixerGain = 0x10,
    MixerEnabled = 0x11,
    Identify = 0x12,
    FrontPanelOversAutoClear = 0x13,
    ClearOvers = 0x14,
    OutputAttenuation = 0x15,
    UnmuteMutesMain = 0x16,
    UnmuteMutesHeadphones = 0x17,
    MuteMutesMain = 0x18,
    MuteMutesHeadphones = 0x19,
    TestMode = 0x1A,
    MetersShowInput = 0x1B,
    FireWireLoopback = 0x1C,

    /// The recovered daemon calls this LimitedGainRange. The active codec has
    /// historically named the same byte InClickless; do not treat those names
    /// as equivalent until physical behaviour has been verified.
    LimitedGainRange = 0x1E,
    ClearResetDefaultRequest = 0x1F,

    PlaybackLatency = 0x21,
    MetersFollowSelection = 0x22,
    MuteInputs = 0x23,
    HasDaemon = 0x24,
    SelectEncoderControl = 0x25,
};

} // namespace ASFW::Audio::Oxford::Apogee
