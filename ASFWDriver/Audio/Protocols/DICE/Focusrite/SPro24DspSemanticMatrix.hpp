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

/// The exact two native cells that implement a verified SPro stereo source
/// into a verified stereo mixer-output pair.  The transaction layer owns
/// their DICE addresses; this profile helper only resolves semantic groups.
struct SPro24DspStereoStripLayout final {
    uint8_t inputLeft{0};
    uint8_t inputRight{0};
    uint8_t outputLeft{0};
    uint8_t outputRight{0};
};

/// The two native cells that implement one mono source's level and pan into
/// one verified stereo destination. Both cells share the same mixer input.
struct SPro24DspMonoStripLayout final {
    uint8_t input{0};
    uint8_t outputLeft{0};
    uint8_t outputRight{0};
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

/// Resolves only a real source L/R pair and mixer-output L/R pair. Mono rows
/// deliberately return no layout: their pan law is a different vendor action.
[[nodiscard]] std::optional<SPro24DspStereoStripLayout>
ResolveSPro24DspStereoStrip(const DiceMixerCoefficients& coefficients,
                            const DiceRouterEntries& routes,
                            DiceRateMode rateMode,
                            uint32_t outputPresentationGroupId,
                            uint32_t inputPresentationGroupId) noexcept;

/// Resolves one real mono source and one output L/R pair. The profile's
/// crosspoint-presentation map is authoritative: hidden/self-send cells can
/// never be reached merely by presenting a known group ID.
[[nodiscard]] std::optional<SPro24DspMonoStripLayout>
ResolveSPro24DspMonoStrip(const DiceMixerCoefficients& coefficients,
                          const DiceRouterEntries& routes,
                          DiceRateMode rateMode,
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
