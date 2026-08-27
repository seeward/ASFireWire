// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "DICERouterMixerTopology.hpp"

#include <limits>

namespace ASFW::Audio::DICE {

namespace {

[[nodiscard]] bool MixerInputIndex(const DiceRouterEntry& route,
                                   uint8_t inputCount,
                                   uint8_t& outIndex) noexcept {
    uint16_t index = 0;
    if (route.destinationBlock == kDiceRouterDestinationMixerTx0) {
        index = route.destinationChannel;
    } else if (route.destinationBlock == kDiceRouterDestinationMixerTx1) {
        index = uint16_t{kDiceMixerTx0ChannelCount} + route.destinationChannel;
    } else {
        return false;
    }
    if (index >= inputCount) return false;
    outIndex = static_cast<uint8_t>(index);
    return true;
}

} // namespace

bool BuildDiceRouterMixerTopology(const DiceMixerCoefficients& coefficients,
                                  const DiceRouterEntries& routes,
                                  DiceRouterMixerTopology& outTopology) noexcept {
    if (coefficients.inputCount == 0 || coefficients.outputCount == 0 ||
        coefficients.inputCount > kDiceMaximumMixerInputs ||
        coefficients.outputCount > kDiceMaximumMixerOutputs ||
        routes.count > kDiceMaximumRouterEntries) {
        outTopology = {};
        return false;
    }

    DiceRouterMixerTopology topology{};
    topology.inputCount = coefficients.inputCount;
    topology.outputCount = coefficients.outputCount;

    for (uint16_t routeIndex = 0; routeIndex < routes.count; ++routeIndex) {
        const auto& route = routes.At(routeIndex);

        uint8_t inputIndex = 0;
        if (MixerInputIndex(route, coefficients.inputCount, inputIndex)) {
            auto& binding = topology.inputs[inputIndex];
            if (binding.routed) {
                outTopology = {};
                return false;
            }
            binding = {.routed = true, .route = route};
        }

        if (route.sourceBlock == kDiceRouterSourceMixer &&
            route.sourceChannel < coefficients.outputCount) {
            auto& count = topology.outputRouteCounts[route.sourceChannel];
            if (count != std::numeric_limits<uint8_t>::max()) ++count;
        }
    }

    outTopology = topology;
    return true;
}

} // namespace ASFW::Audio::DICE
