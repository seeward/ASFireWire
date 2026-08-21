// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioClockCommand.hpp — the vendor command that tells M-Audio "special"
// firmware which clock to run on and which digital formats are selected.
//
// This is the first half of the vendor driver's blank-slate configuration.
// Linux calls it the very first thing in discovery and treats failure as fatal,
// under the comment "initialize these parameters because driver is not allowed
// to ask"
// (bebob_maudio.c:273-281). Without it the device is never told to clock
// internally, and a device that is not clocked does not transmit — which is
// exactly what a silent IR context looks like.
//
// Closed by construction, like BeBoBBootloaderCue and AVCSignalFormatProbe: the
// company ID has no parameter. That is the whole safety boundary here. BridgeCo's
// own extensions are also vendor-dependent, also opcode 0x00, and freeze this
// firmware; only bytes 3..5 separate this command from the ones that hang it.
//
//   references/linux-sound-firewire-stack/firewire/bebob/bebob_maudio.c:171-198
//     avc_maudio_set_special_clk() — same 12 meaningful bytes
//   vendor kext com_m_audio_FW1814Device::SetClockSourceInternal (0xe25c)
//     builds the same prefix and operands in a 16-byte zero-padded frame

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ASFW::Audio::BeBoB {

inline constexpr size_t kMAudioClockCommandBytes = 16;

/// The second half of SetBlankSlateClockSource selects the default S/PDIF input
/// interface through Audio-subunit selector function block 4. The original
/// driver maps its blank-slate word to selector value zero, producing:
///
///   00 08 b8 80 04 10 02 00 01 00 00 00
///
/// Linux independently identifies function block 4 as the 1814/ProjectMix
/// digital-input-interface selector (bebob_maudio.c:461-462, 514).
///
/// NOT SENT under the current policy. These describe the vendor's operand-0
/// path, which pairs the selector with clock source `InternalDigitalMute` and
/// issues both against an already-streaming output plug. ASFW follows Linux
/// instead — source `Internal`, no selector — so nothing here has a call site;
/// they remain as the record of the alternative. See MAudioSpecialProtocol's
/// InitializeClock for why the two policies must not be mixed.
inline constexpr uint8_t kMAudioDigitalInputSelectorBlockId = 0x04;
inline constexpr uint8_t kMAudioDefaultDigitalInputInterface = 0x00;

/// Clock source selector. These are indices into the device's own list, not a
/// bitfield — Linux exposes them as an enum whose labels name each one
/// (bebob_maudio.c:343-364).
enum class MAudioClockSource : uint8_t {
    /// Internal, with the digital outputs muted. What the vendor kext selects
    /// in SetBlankSlateClockSource.
    InternalDigitalMute = 0,
    /// S/PDIF or ADAT, whichever dig_in_fmt selects.
    Digital = 1,
    WordClock = 2,
    /// Plain internal. What Linux selects at discovery.
    Internal = 3,
};

/// Digital interface format for one direction. Same wire values as
/// MAudioDigitalFormat in MAudioSpecialFormation.hpp; kept separate here so this
/// header stays a pure frame builder.
enum class MAudioClockDigitalFormat : uint8_t {
    SPDIF = 0,
    ADAT = 1,
};

/// Builds the CONTROL frame. Every caller-supplied value is an operand — none of
/// them can become an opcode or a company ID.
[[nodiscard]] constexpr std::array<uint8_t, kMAudioClockCommandBytes>
BuildMAudioClockCommand(MAudioClockSource source,
                        MAudioClockDigitalFormat captureFormat,
                        MAudioClockDigitalFormat playbackFormat,
                        bool lockSettings) noexcept {
    return {
        0x00,  // AV/C CONTROL
        0xFF,  // unit
        0x00,  // VENDOR DEPENDENT
        0x04,  // company ID high   — pinned; this is the safety boundary
        0x00,  // company ID middle
        0x04,  // company ID low
        static_cast<uint8_t>(source),
        static_cast<uint8_t>(captureFormat),   // dig_in_fmt  -> selects capture geometry
        static_cast<uint8_t>(playbackFormat),  // dig_out_fmt -> selects playback geometry
        lockSettings ? uint8_t{0x01} : uint8_t{0x00},
        0x00,  // operand padding, also zeroed by Linux
        0x00,
        0x00,  // vendor-kext frame padding; FireBug records all 16 bytes
        0x00,
        0x00,
        0x00,
    };
}

inline constexpr size_t kMAudioLedCommandBytes = 8;

/// Builds the front-panel LED command.
///
/// The 1814's LED is not autonomous. The device reports its momentary switch in
/// the meter block and the host decides what the lamp shows; nothing lights it
/// otherwise. The ALSA runtime states the whole policy in three lines — when the
/// polled switch bit changes, send the new state
/// (runtime/bebob/src/maudio/special_model.rs:159-163).
///
/// Closed by construction like the clock command above: the company ID is
/// pinned, and the only caller-supplied value is a boolean operand.
///
///   vendor kext com_m_audio_FW1814Device::AVCControlSetLEDStatus (0xdaaa)
///     — 8-byte frame, OUI 03 00 01, state at operand 0, zero pad
///   references/alsa-userspace-control-protocols-impl/protocols/bebob/src/maudio/special.rs:121-167
///     — MaudioSpecialLedSwitch, same OUI. It pads with 0xff where the vendor
///       pads with 0x00; we follow the vendor.
[[nodiscard]] constexpr std::array<uint8_t, kMAudioLedCommandBytes>
BuildMAudioLedCommand(bool illuminated) noexcept {
    return {
        0x00,  // AV/C CONTROL
        0xFF,  // unit
        0x00,  // VENDOR DEPENDENT
        0x03,  // company ID high   — pinned; this is the safety boundary
        0x00,  // company ID middle
        0x01,  // company ID low
        illuminated ? uint8_t{0x01} : uint8_t{0x00},
        0x00,  // pad, as the vendor sends it
    };
}

/// SetClockSourceInternal waits 300 ms between its vendor frame and the Audio
/// selector command, then another 300 ms after the selector completes.
inline constexpr uint32_t kMAudioClockToSelectorInterlockMs = 300;
inline constexpr uint32_t kMAudioClockSelectorSettleMs = 300;

/// How long SetBlankSlateClockSource waits after SetClockSourceInternal returns.
///
/// 2500 ms, from the vendor kext's SetBlankSlateClockSource, after the nested
/// SetClockSourceInternal clock/selector sequence returns. This is in addition
/// to that function's two 300 ms waits.
///
/// The complete 3100 ms blank-slate sequence belongs at install time, not in the
/// stream-start path: with the signal-format interlock and the device's own
/// roughly 1 s transmission delay it can exceed the 4 s initial-ZTS budget, and
/// a working device would look like a hung one.
inline constexpr uint32_t kMAudioClockSettleMs = 2500;

/// No wire operation occurs between the second 300 ms wait and the outer
/// 2500 ms wait, so the asynchronous implementation may coalesce them without
/// changing observable bus behaviour.
inline constexpr uint32_t kMAudioPostSelectorSettleMs =
    kMAudioClockSelectorSettleMs + kMAudioClockSettleMs;

} // namespace ASFW::Audio::BeBoB
