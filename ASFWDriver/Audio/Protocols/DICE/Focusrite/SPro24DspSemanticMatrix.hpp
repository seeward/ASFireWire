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

struct SPro24DspStereoStripCoefficients final {
    uint16_t left{0};
    uint16_t right{0};
};

[[nodiscard]] bool BuildSPro24DspSemanticMatrix(
    const DiceMixerCoefficients& coefficients,
    const DiceRouterEntries& routes,
    AudioSemanticMatrixSnapshot& outSnapshot) noexcept;

/// Resolves only a real source L/R pair and mixer-output L/R pair. Mono rows
/// deliberately return no layout: their pan law is a different vendor action.
[[nodiscard]] std::optional<SPro24DspStereoStripLayout>
ResolveSPro24DspStereoStrip(const DiceMixerCoefficients& coefficients,
                            const DiceRouterEntries& routes,
                            uint32_t outputPresentationGroupId,
                            uint32_t inputPresentationGroupId) noexcept;

/// Clean-room reproduction of the vendor's captured stereo balance law.
/// Level is supplied in physical dB; balance is -1000…+1000.
[[nodiscard]] std::optional<SPro24DspStereoStripCoefficients>
MakeSPro24DspStereoStripCoefficients(int32_t levelMilliDb,
                                     int32_t balanceMilli) noexcept;

} // namespace ASFW::Audio::DICE::Focusrite
