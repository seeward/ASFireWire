// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSemanticMatrix.hpp"

namespace ASFW::Audio::DICE::Focusrite {

namespace {

constexpr uint32_t kMixerInputPortBase = 0x5352'0000;
constexpr uint32_t kMixerOutputPortBase = 0x5353'0000;

// TCAT's router destinations for the 18 mixer inputs. These are protocol
// facts, kept private to the profile and cross-validated with the local
// tcd22xx reference's MixerTx0 (16) + MixerTx1 (2) split.
constexpr uint8_t kRouterDestinationMixerTx0 = 2;
constexpr uint8_t kRouterDestinationMixerTx1 = 3;

AudioSemanticSignalKind SignalKindForSource(uint8_t block) noexcept {
    // Source-block vocabulary is deliberately translated here, rather than
    // exported as a DICE enum. Unknown/mute routes fall back to a stable
    // internal auxiliary port while retaining the matrix input position.
    switch (block) {
    case 0: return AudioSemanticSignalKind::DigitalSpdif;
    case 1: return AudioSemanticSignalKind::DigitalAdat;
    case 4:
    case 5: return AudioSemanticSignalKind::AnalogLine;
    case 11:
    case 12: return AudioSemanticSignalKind::HostStream;
    case 2: return AudioSemanticSignalKind::Auxiliary;
    default: return AudioSemanticSignalKind::Auxiliary;
    }
}

uint32_t SignalIndexForSource(const DiceRouterEntry& entry) noexcept {
    // Signal indices are one-based user-facing channel numbers. Distinct TCAT
    // source blocks of one signal kind need disjoint identities as well.
    switch (entry.sourceBlock) {
    case 4: return uint32_t{entry.sourceChannel} + 1U;
    case 5: return uint32_t{entry.sourceChannel} + 17U;
    case 11: return uint32_t{entry.sourceChannel} + 1U;
    case 12: return uint32_t{entry.sourceChannel} + 17U;
    default: return uint32_t{entry.sourceChannel} + 1U;
    }
}

bool FindMixerInputRoute(const DiceRouterEntries& routes,
                         uint32_t input,
                         DiceRouterEntry& outEntry) noexcept {
    const uint8_t destinationBlock = input < 16U ? kRouterDestinationMixerTx0
                                                  : kRouterDestinationMixerTx1;
    const uint8_t destinationChannel = static_cast<uint8_t>(input < 16U ? input : input - 16U);
    for (uint16_t route = 0; route < routes.count; ++route) {
        const auto& candidate = routes.At(route);
        if (candidate.destinationBlock == destinationBlock &&
            candidate.destinationChannel == destinationChannel) {
            outEntry = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

bool BuildSPro24DspSemanticMatrix(const DiceMixerCoefficients& coefficients,
                                  const DiceRouterEntries& routes,
                                  AudioSemanticMatrixSnapshot& outSnapshot) noexcept {
    if (coefficients.inputCount == 0 || coefficients.outputCount == 0 ||
        coefficients.inputCount > kMaxAudioSemanticMatrixInputs ||
        coefficients.outputCount > kMaxAudioSemanticMatrixOutputs) {
        return false;
    }

    outSnapshot = {};
    outSnapshot.deviceKind = kSPro24DspSemanticDeviceKind;
    // The coordinator substitutes the endpoint's live revision at publication.
    outSnapshot.topologyRevision = 1;
    outSnapshot.kind = AudioSemanticMatrixKind::Mixer;
    outSnapshot.inputCount = coefficients.inputCount;
    outSnapshot.outputCount = coefficients.outputCount;
    outSnapshot.coefficientMaximum = 65535;

    for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
        DiceRouterEntry route{};
        const bool found = FindMixerInputRoute(routes, input, route);
        outSnapshot.inputs[input] = {
            .portId = kMixerInputPortBase + input + 1U,
            .signalKind = found ? SignalKindForSource(route.sourceBlock)
                                : AudioSemanticSignalKind::Auxiliary,
            .signalIndex = found ? SignalIndexForSource(route) : input + 1U,
        };
    }
    for (uint32_t output = 0; output < coefficients.outputCount; ++output) {
        outSnapshot.outputs[output] = {
            .portId = kMixerOutputPortBase + output + 1U,
            .signalKind = AudioSemanticSignalKind::Auxiliary,
            .signalIndex = output + 1U,
        };
        for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
            outSnapshot.coefficients[size_t{output} * kMaxAudioSemanticMatrixInputs + input] =
                coefficients.At(static_cast<uint8_t>(output), static_cast<uint8_t>(input));
        }
    }
    return ValidateAudioSemanticMatrix(outSnapshot).has_value();
}

} // namespace ASFW::Audio::DICE::Focusrite
