// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DICERouterMixerTopology.hpp -- generic TCAT router/mixer binding.
//
// The TCAT mixer section is an anonymous scalar matrix. Its input identities
// and the mixer outputs which currently reach another hardware block are
// established by the active router image, not by the coefficient window.

#pragma once

#include "DICEExtensionState.hpp"

#include <array>
#include <cstdint>

namespace ASFW::Audio::DICE {

// TCAT TCD22xx router block identifiers. These are protocol vocabulary and
// must be translated into device semantics before crossing the driver/UI seam.
// Cross-validated with the local ALSA control reference:
// protocols/dice/src/tcat/extension/router_entry.rs:12-20,89-97 and
// protocols/dice/src/tcat/tcd22xx_spec.rs:52-56.
inline constexpr uint8_t kDiceRouterDestinationMixerTx0 = 2;
inline constexpr uint8_t kDiceRouterDestinationMixerTx1 = 3;
inline constexpr uint8_t kDiceRouterSourceMixer = 2;
inline constexpr uint8_t kDiceMixerTx0ChannelCount = 16;

struct DiceMixerInputBinding final {
    bool routed{false};
    DiceRouterEntry route{};
};

/// Protocol-level relation between one active router image and one mixer
/// coefficient image. It deliberately carries no product names, stereo
/// grouping, pan law, or physical-output policy.
struct DiceRouterMixerTopology final {
    uint8_t inputCount{0};
    uint8_t outputCount{0};
    std::array<DiceMixerInputBinding, kDiceMaximumMixerInputs> inputs{};
    /// Number of active router entries which consume each mixer output.
    /// Zero means that the row is computed but currently reaches no router
    /// destination; it is not a usable bus in the active topology.
    std::array<uint8_t, kDiceMaximumMixerOutputs> outputRouteCounts{};

    [[nodiscard]] constexpr bool OutputIsRouted(uint8_t output) const noexcept {
        return output < outputCount && outputRouteCounts[output] != 0;
    }
};

/// Joins TCAT's fixed MixerTx0/MixerTx1 input ports and Mixer source block to
/// the active coefficient geometry. Duplicate routes to one mixer input fail
/// closed because they make source identity ambiguous.
[[nodiscard]] bool BuildDiceRouterMixerTopology(
    const DiceMixerCoefficients& coefficients,
    const DiceRouterEntries& routes,
    DiceRouterMixerTopology& outTopology) noexcept;

} // namespace ASFW::Audio::DICE
