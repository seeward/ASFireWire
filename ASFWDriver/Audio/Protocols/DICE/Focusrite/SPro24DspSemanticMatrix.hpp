// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// SPro24DspSemanticMatrix.hpp -- Saffire Pro 24 DSP mixer projection.
//
// TCAT router block/channel fields are decoded below the semantic boundary.
// The resulting matrix axes identify only stable mixer ports and the current
// source signal class; the UI receives none of the vendor block IDs.

#pragma once

#include "../Core/DICEExtensionState.hpp"
#include "../Core/DICETypes.hpp"
#include "../../../Shared/Topology/IAudioSemanticMatrix.hpp"

#include <optional>

namespace ASFW::Audio::DICE::Focusrite {

inline constexpr uint32_t kSPro24DspSemanticDeviceKind = 0x5350'3234; // "SP24"

/// The native mixer cells behind one semantic strip. A mono strip drives both
/// destination rows from a single source, so its two input cells are equal;
/// that is the only structural difference from a stereo strip, and it is what
/// selects the pan law rather than the balance law.
struct SPro24DspStripCells final {
    uint8_t outputLeft{0};
    uint8_t outputRight{0};
    uint8_t inputLeft{0};
    uint8_t inputRight{0};
    bool mono{false};
};

struct SPro24DspStereoStripCoefficients final {
    uint16_t left{0};
    uint16_t right{0};
};

/// `rateMode` must be the mode whose CURRENT_CONFIG router image `routes` was
/// read from. The vendor signal table is rate-scoped -- both DSP return pairs
/// move router channel between 1x and 2x -- so resolving a source against the
/// wrong mode mislabels them rather than failing.
[[nodiscard]] bool BuildSPro24DspSemanticMatrix(
    const DiceMixerCoefficients& coefficients,
    const DiceRouterEntries& routes,
    DiceRateMode rateMode,
    AudioSemanticMatrixSnapshot& outSnapshot) noexcept;

/// Maps a published strip back to the native cells behind it. The snapshot's
/// stable port IDs carry the native row and column, which is what stops a
/// compacted semantic index from addressing the wrong hardware cell. The
/// profile's crosspoint-presentation map remains authoritative: hidden and
/// self-send cells are unreachable even with a known group ID in hand.
[[nodiscard]] std::optional<SPro24DspStripCells> ResolveSPro24DspStripCells(
    const AudioSemanticMatrixSnapshot& snapshot,
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId) noexcept;

/// Clean-room reproduction of the vendor's captured stereo balance law.
/// Level is supplied in physical dB; balance is -1000…+1000.
[[nodiscard]] std::optional<SPro24DspStereoStripCoefficients>
MakeSPro24DspStereoStripCoefficients(int32_t levelMilliDb,
                                     int32_t balanceMilli) noexcept;

/// ASFW-defined constant-power mono pan. This is deliberately distinct from
/// the captured vendor stereo balance curve: centre attenuates both cells by
/// approximately 3.01 dB and hard endpoints write an exact zero.
[[nodiscard]] std::optional<SPro24DspStereoStripCoefficients>
MakeSPro24DspMonoStripCoefficients(int32_t levelMilliDb,
                                   int32_t panMilli) noexcept;

} // namespace ASFW::Audio::DICE::Focusrite
