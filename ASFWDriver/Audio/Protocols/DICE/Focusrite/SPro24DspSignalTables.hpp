// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// SPro24DspSignalTables.hpp -- the vendor's own signal names, both directions.
//
// Decoded from MixControl's `Pro24DSP_IpSigTab` (41 sources) and
// `Pro24DSP_OpSigTab` (47 destinations). Each vendor entry names one signal and
// carries a separate router (block, channel) per rate mode, because several
// signals move channel between 1x and 2x:
//
//   FX(Anlg 1/2)  Ins0:8/9   -> Ins0:4/5      Line 5/6   Ins0:4/5  -> Ins0:8/9
//   FmRvb 0/1     Ins0:14/15 -> Ins0:6/7      ToRvb 0/1  Ins0:14/15-> Ins0:6/7
//   ToFX 0/1      Ins0:8/9   -> Ins0:4/5      Loop. 1/2  Avs0:14/15-> Avs0:10/11
//
// Line 5/6 and ToFX 0/1 exchange positions outright, so a decoder that hardcodes
// the 1x channels does not merely lose a label above 48 kHz -- it reports the
// channel-strip send as a line output and the line output as a channel-strip
// send. Consulting the table is the only way to be right at both rates.
//
// Block IDs are TCAT vocabulary, cross-validated with the local ALSA control
// reference, protocols/dice/src/tcat/extension/router_entry.rs:6-20,81-95.
// Sources: Aes=0, Adat=1, Mixer=2, Ins0=4, Ins1=5, ArmAprAudio=10, Avs0=11,
// Avs1=12, Mute=15. Destinations differ: MixerTx0=2 and MixerTx1=3 take the
// slots the source vocabulary gives to Mixer.

#pragma once

#include "../Core/DICETypes.hpp"
#include "../../../Shared/Topology/IAudioSemanticTopology.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ASFW::Audio::DICE::Focusrite {

/// A router block field is four bits wide, so this can never match a real route
/// and a signal absent at a rate is unreachable by construction.
inline constexpr uint8_t kSPro24SignalAbsent = 0xFF;
inline constexpr size_t kSPro24RateModeCount = 3;

/// Source block IDs.
inline constexpr uint8_t kSPro24BlockAes = 0;
inline constexpr uint8_t kSPro24BlockAdat = 1;
inline constexpr uint8_t kSPro24BlockMixer = 2;
inline constexpr uint8_t kSPro24BlockIns0 = 4;
inline constexpr uint8_t kSPro24BlockArmApr = 10;
inline constexpr uint8_t kSPro24BlockAvs0 = 11;
inline constexpr uint8_t kSPro24BlockMute = 15;
/// Destination-only block IDs.
inline constexpr uint8_t kSPro24BlockMixerTx0 = 2;
inline constexpr uint8_t kSPro24BlockMixerTx1 = 3;
/// "Off" as a destination: a route that reaches nowhere. The captured image
/// uses it twice, sending Mixer:0/1 to it purely so they occupy a peak slot.
inline constexpr uint8_t kSPro24BlockOffDestination = 15;

/// Several distinct vendor categories collapse onto `Auxiliary`, which carries
/// no sub-kind, so their signal indices must not overlap. The two directions are
/// separate tables and therefore separate index spaces.
inline constexpr uint32_t kSPro24AuxSourceEffectReturnFirst = 1;   // FX(Anlg 1/2)
inline constexpr uint32_t kSPro24AuxSourceReverbReturnFirst = 3;   // FmRvb 0/1
inline constexpr uint32_t kSPro24AuxSourceMixReturnFirst = 5;      // FromMix1..8
inline constexpr uint32_t kSPro24AuxSourceReverbSendFirst = 13;    // RvbSend-1/2
inline constexpr uint32_t kSPro24AuxSourceArmFirst = 15;           // FromArm-0/1
inline constexpr uint32_t kSPro24AuxSourceMuted = 17;              // Off

inline constexpr uint32_t kSPro24AuxDestinationEffectSendFirst = 1;   // ToFX 0/1
inline constexpr uint32_t kSPro24AuxDestinationReverbSendFirst = 3;   // ToRvb 0/1
inline constexpr uint32_t kSPro24AuxDestinationLoopbackFirst = 5;     // Loop. 1/2
inline constexpr uint32_t kSPro24AuxDestinationMixerInputFirst = 7;   // ToMix1..18
inline constexpr uint32_t kSPro24AuxDestinationOff = 25;              // Off

/// One vendor table entry. `signalIndex` is one-based, user-facing, and disjoint
/// within its kind. `stereoPair` is set only where the vendor pairs two entries
/// as one stereo signal, never inferred from adjacent numbering.
struct SPro24Signal final {
    AudioSemanticSignalKind kind{AudioSemanticSignalKind::None};
    uint32_t signalIndex{0};
    bool stereoPair{false};
    /// Router coordinates indexed by DiceRateMode. The Pro 24 DSP publishes
    /// 44.1/48/88.2/96 kHz only, so the High column is absent throughout rather
    /// than guessed; a High-rate lookup finds nothing and fails closed.
    std::array<uint8_t, kSPro24RateModeCount> block{};
    std::array<uint8_t, kSPro24RateModeCount> channel{};
};

/// The vendor entry naming the source at `block:channel` for `rateMode`, or
/// null when the active router points at something the table does not describe.
[[nodiscard]] const SPro24Signal* FindSPro24InputSignal(uint8_t sourceBlock,
                                                         uint8_t sourceChannel,
                                                         DiceRateMode rateMode) noexcept;

/// The vendor entry naming the destination at `block:channel` for `rateMode`.
[[nodiscard]] const SPro24Signal* FindSPro24OutputSignal(uint8_t destinationBlock,
                                                          uint8_t destinationChannel,
                                                          DiceRateMode rateMode) noexcept;

/// Router coordinates of one table entry at a rate, or false when it is absent
/// there. Used to build a destination list without re-deriving channel numbers.
[[nodiscard]] bool SPro24SignalCoordinates(const SPro24Signal& signal,
                                            DiceRateMode rateMode,
                                            uint8_t& outBlock,
                                            uint8_t& outChannel) noexcept;

/// All 41 source entries, in vendor order.
[[nodiscard]] std::span<const SPro24Signal> SPro24InputSignals() noexcept;
/// All 47 destination entries, in vendor order.
[[nodiscard]] std::span<const SPro24Signal> SPro24OutputSignals() noexcept;

} // namespace ASFW::Audio::DICE::Focusrite
