// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSemanticMatrix.hpp"

#include "../Core/DICERouterMixerTopology.hpp"
#include "../../../Shared/Topology/AudioSemanticMatrixStripStates.hpp"
#include "SPro24DspSignalTables.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ASFW::Audio::DICE::Focusrite {

namespace {

constexpr uint32_t kMixerInputPortBase = 0x5352'0000;
constexpr uint32_t kMixerOutputPortBase = 0x5353'0000;
constexpr uint32_t kMixerInputPresentationGroupBase = 0x5354'0000;
constexpr uint32_t kMixerOutputPresentationGroupBase = 0x5355'0000;

// Product presentation is applied only after the generic TCAT layer has
// joined the active router and anonymous 18 x 16 coefficient window.
// Signal identity -- both vendor tables -- lives in SPro24DspSignalTables so the
// patchbay can name destinations and sources from the same source of truth.
using Kind = AudioSemanticSignalKind;
using SPro24InputSignal = SPro24Signal;


struct InputPresentation final {
    uint32_t groupId{0};
    AudioSemanticMatrixChannelRole role{AudioSemanticMatrixChannelRole::Mono};
};

InputPresentation PresentationForSource(const SPro24InputSignal* signal,
                                        Kind kind,
                                        uint32_t signalIndex,
                                        uint32_t input) noexcept {
    // Only sources the vendor table pairs receive L/R roles. ADAT and the
    // individual analog/strip sources stay mono even where their labels are
    // numerically adjacent: pairing them from index coincidence is exactly the
    // UI bug this semantic field exists to avoid.
    if (signal == nullptr || !signal->stereoPair) {
        return {.groupId = kMixerInputPresentationGroupBase + input + 1U,
                .role = AudioSemanticMatrixChannelRole::Mono};
    }

    const uint32_t pairStart = (signalIndex - 1U) & ~uint32_t{1};
    const uint32_t kindOffset = static_cast<uint32_t>(kind) << 8U;
    return {.groupId = kMixerInputPresentationGroupBase + kindOffset + pairStart + 1U,
            .role = (signalIndex & 1U) != 0U ? AudioSemanticMatrixChannelRole::Left
                                              : AudioSemanticMatrixChannelRole::Right};
}

/// Reads the published axis rather than the raw route, so it stays correct at
/// the rate where the reverb return moves from Ins0:14/15 to Ins0:6/7.
[[nodiscard]] bool IsSPro24ReverbReturn(const AudioSemanticMatrixAxis& axis) noexcept {
    return axis.signalKind == Kind::Auxiliary &&
           (axis.signalIndex == kSPro24AuxSourceReverbReturnFirst ||
            axis.signalIndex == kSPro24AuxSourceReverbReturnFirst + 1U);
}

[[nodiscard]] AudioSemanticMatrixOutputRole OutputRoleForSPro24Row(
    uint8_t rawOutput) noexcept {
    // MixControl's recovered output table exposes Mixer:0..7 as four monitor
    // pairs and Mixer:8/9 as the reverb-send pair. Rows 10..15 are computed by
    // TCAT but have no SPro24 product destination and remain unpublished.
    if (rawOutput < 8U) return AudioSemanticMatrixOutputRole::MonitorMix;
    if (rawOutput < 10U) return AudioSemanticMatrixOutputRole::EffectSend;
    return AudioSemanticMatrixOutputRole::None;
}

[[nodiscard]] std::optional<uint8_t> RawInputForPortId(uint32_t portId) noexcept {
    if (portId <= kMixerInputPortBase ||
        portId > kMixerInputPortBase + kDiceMaximumMixerInputs) return std::nullopt;
    return static_cast<uint8_t>(portId - kMixerInputPortBase - 1U);
}

[[nodiscard]] std::optional<uint8_t> RawOutputForPortId(uint32_t portId) noexcept {
    if (portId <= kMixerOutputPortBase ||
        portId > kMixerOutputPortBase + kDiceMaximumMixerOutputs) return std::nullopt;
    return static_cast<uint8_t>(portId - kMixerOutputPortBase - 1U);
}

[[nodiscard]] float FaderLawDb(float normalized,
                                float minimumDb,
                                float maximumDb,
                                float base) noexcept {
    const float clamped = std::clamp(normalized, 0.0F, 1.0F);
    const float fraction = (std::pow(base, clamped) - 1.0F) / (base - 1.0F);
    return minimumDb + fraction * (maximumDb - minimumDb);
}

[[nodiscard]] float BalanceAttenuationDb(float normalized) noexcept {
    // Independently recovered from MixControl's BalanceLaw::fader2dB.  The
    // curved half maps silence…-0.01 dB, then the final 0.01 dB reaches
    // exact unity.  This is deliberately not a generic equal-power pan.
    if (normalized > 0.5F) {
        return -0.01F + ((normalized - 0.5F) * 2.0F) * 0.01F;
    }
    return FaderLawDb(normalized * 2.0F, -80.0F, -0.01F, 0.002770087F);
}

[[nodiscard]] uint16_t Q214ForDb(float db) noexcept {
    constexpr float kMuteFloorDb = -85.0F;
    constexpr float kMaximumDb = 12.0F;
    if (db <= kMuteFloorDb) return 0;
    const float amplitude = std::pow(10.0F, std::min(db, kMaximumDb) / 20.0F);
    const auto encoded = static_cast<uint32_t>(std::lround(amplitude * 16384.0F));
    return static_cast<uint16_t>(std::min(encoded, uint32_t{0xffff}));
}

} // namespace

bool BuildSPro24DspSemanticMatrix(const DiceMixerCoefficients& coefficients,
                                  const DiceRouterEntries& routes,
                                  DiceRateMode rateMode,
                                  AudioSemanticMatrixSnapshot& outSnapshot) noexcept {
    if (coefficients.inputCount == 0 || coefficients.outputCount == 0 ||
        coefficients.inputCount > kMaxAudioSemanticMatrixInputs ||
        coefficients.outputCount > kMaxAudioSemanticMatrixOutputs) {
        return false;
    }

    DiceRouterMixerTopology topology{};
    if (!BuildDiceRouterMixerTopology(coefficients, routes, topology)) return false;

    outSnapshot = {};
    outSnapshot.deviceKind = kSPro24DspSemanticDeviceKind;
    // The coordinator substitutes the endpoint's live revision at publication.
    outSnapshot.topologyRevision = 1;
    outSnapshot.kind = AudioSemanticMatrixKind::Mixer;
    outSnapshot.inputCount = coefficients.inputCount;
    outSnapshot.outputCount = 0;
    outSnapshot.coefficientMaximum = 65535;
    outSnapshot.gainLaw = AudioSemanticMatrixGainLaw::UnsignedQ214Amplitude;

    for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
        const auto& binding = topology.inputs[input];
        // An unrouted input, or one the vendor table does not describe at this
        // rate, keeps its matrix position and stays an unnamed mono auxiliary
        // rather than borrowing a neighbour's identity.
        const auto* signal = binding.routed
            ? FindSPro24InputSignal(binding.route.sourceBlock,
                                    binding.route.sourceChannel, rateMode)
            : nullptr;
        const auto kind = signal ? signal->kind : AudioSemanticSignalKind::Auxiliary;
        const uint32_t signalIndex = signal ? signal->signalIndex : input + 1U;
        const auto presentation = PresentationForSource(signal, kind, signalIndex, input);
        outSnapshot.inputs[input] = {
            .portId = kMixerInputPortBase + input + 1U,
            .signalKind = kind,
            .signalIndex = signalIndex,
            .presentationGroupId = presentation.groupId,
            .channelRole = presentation.role,
        };
    }
    for (uint8_t rawOutput = 0; rawOutput < coefficients.outputCount; ++rawOutput) {
        if (!topology.OutputIsRouted(rawOutput)) continue;
        const auto role = OutputRoleForSPro24Row(rawOutput);
        if (role == AudioSemanticMatrixOutputRole::None) continue;

        const uint32_t output = outSnapshot.outputCount++;
        outSnapshot.outputs[output] = {
            .portId = kMixerOutputPortBase + rawOutput + 1U,
            .signalKind = AudioSemanticSignalKind::Auxiliary,
            .signalIndex = role == AudioSemanticMatrixOutputRole::EffectSend
                ? uint32_t{rawOutput} - 7U
                : uint32_t{rawOutput} + 1U,
            .presentationGroupId = kMixerOutputPresentationGroupBase +
                ((rawOutput & ~uint32_t{1}) + 1U),
            .channelRole = (rawOutput & 1U) == 0U ? AudioSemanticMatrixChannelRole::Left
                                                   : AudioSemanticMatrixChannelRole::Right,
            .outputRole = role,
        };
        for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
            outSnapshot.coefficients[size_t{output} * kMaxAudioSemanticMatrixInputs + input] =
                coefficients.At(rawOutput, static_cast<uint8_t>(input));

            auto presentation = AudioSemanticMatrixCrosspointPresentation::ScalarReadback;
            const auto inputRole = outSnapshot.inputs[input].channelRole;
            const auto outputChannelRole = outSnapshot.outputs[output].channelRole;
            if (!topology.inputs[input].routed) {
                // A coefficient exists for every matrix cell, but an
                // unbound input row has no stable semantic source identity.
                // Preserve it as diagnostic readback and fail closed for
                // grouped writes.
                presentation = AudioSemanticMatrixCrosspointPresentation::ScalarReadback;
            } else if (role == AudioSemanticMatrixOutputRole::EffectSend &&
                IsSPro24ReverbReturn(outSnapshot.inputs[input])) {
                // MixControl's reverb-send source list does not feed the
                // reverb return back into its own input. The scalar cells
                // remain in readback, but exposing them as a strip creates a
                // hardware feedback path with no useful product meaning.
                presentation = AudioSemanticMatrixCrosspointPresentation::Hidden;
            } else if (inputRole == AudioSemanticMatrixChannelRole::Mono) {
                presentation = AudioSemanticMatrixCrosspointPresentation::MonoLevelPan;
            } else if (inputRole == outputChannelRole) {
                presentation = AudioSemanticMatrixCrosspointPresentation::StereoLevelBalance;
            } else {
                // Off-diagonal cells are not members of the vendor's linked
                // stereo level/balance gesture.
                presentation = AudioSemanticMatrixCrosspointPresentation::Hidden;
            }
            outSnapshot.crosspointPresentations[
                size_t{output} * kMaxAudioSemanticMatrixInputs + input] = presentation;
        }
    }
    if (outSnapshot.outputCount == 0) return false;
    return ValidateAudioSemanticMatrix(outSnapshot).has_value();
}

std::optional<SPro24DspStripCells> ResolveSPro24DspStripCells(
    const AudioSemanticMatrixSnapshot& snapshot,
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId) noexcept {
    AudioSemanticMatrixStripCells strip{};
    if (!ResolveAudioSemanticMatrixStrip(snapshot, outputPresentationGroupId,
                                         inputPresentationGroupId, strip)) {
        return std::nullopt;
    }

    // Semantic axis positions are compacted; the native row and column live in
    // the port ID. Writing through the compact index would silently address a
    // different hardware cell.
    const auto outputLeft = RawOutputForPortId(snapshot.outputs[strip.outputLeft].portId);
    const auto outputRight = RawOutputForPortId(snapshot.outputs[strip.outputRight].portId);
    const auto inputLeft = RawInputForPortId(snapshot.inputs[strip.inputLeft].portId);
    const auto inputRight = RawInputForPortId(snapshot.inputs[strip.inputRight].portId);
    if (!outputLeft || !outputRight || !inputLeft || !inputRight) return std::nullopt;

    // The vendor pairs mixer rows as 1/2 through 15/16 and sources as adjacent
    // native columns; a group whose members are not one native pair is not a
    // strip this profile knows how to drive.
    if (*outputRight != static_cast<uint8_t>(*outputLeft + 1U)) return std::nullopt;
    if (!strip.IsMono() && *inputRight != static_cast<uint8_t>(*inputLeft + 1U)) {
        return std::nullopt;
    }

    return SPro24DspStripCells{
        .outputLeft = *outputLeft,
        .outputRight = *outputRight,
        .inputLeft = *inputLeft,
        .inputRight = *inputRight,
        .mono = strip.IsMono(),
    };
}

std::optional<SPro24DspStereoStripCoefficients>
MakeSPro24DspStereoStripCoefficients(int32_t levelMilliDb,
                                     int32_t balanceMilli) noexcept {
    constexpr int32_t kMinimumLevelMilliDb = -85000;
    constexpr int32_t kMaximumLevelMilliDb = 6000;
    constexpr int32_t kMinimumBalanceMilli = -1000;
    constexpr int32_t kMaximumBalanceMilli = 1000;
    if (levelMilliDb < kMinimumLevelMilliDb || levelMilliDb > kMaximumLevelMilliDb ||
        balanceMilli < kMinimumBalanceMilli || balanceMilli > kMaximumBalanceMilli) {
        return std::nullopt;
    }

    const float levelDb = static_cast<float>(levelMilliDb) / 1000.0F;
    // MixControl explicitly writes a zero Q2.14 cell at either hard pan stop.
    // Do not approximate that endpoint through the non-zero -80 dB balance
    // curve: the hardware image uses zero as its mute representation.
    if (balanceMilli == kMinimumBalanceMilli) {
        return SPro24DspStereoStripCoefficients{
            .left = Q214ForDb(levelDb),
            .right = 0,
        };
    }
    if (balanceMilli == kMaximumBalanceMilli) {
        return SPro24DspStereoStripCoefficients{
            .left = 0,
            .right = Q214ForDb(levelDb),
        };
    }
    const float balance = static_cast<float>(balanceMilli - kMinimumBalanceMilli) /
        static_cast<float>(kMaximumBalanceMilli - kMinimumBalanceMilli);
    return SPro24DspStereoStripCoefficients{
        .left = Q214ForDb(levelDb + BalanceAttenuationDb(1.0F - balance)),
        .right = Q214ForDb(levelDb + BalanceAttenuationDb(balance)),
    };
}

std::optional<SPro24DspStereoStripCoefficients>
MakeSPro24DspMonoStripCoefficients(int32_t levelMilliDb,
                                   int32_t panMilli) noexcept {
    constexpr int32_t kMinimumLevelMilliDb = -85000;
    constexpr int32_t kMaximumLevelMilliDb = 6000;
    constexpr int32_t kMinimumPanMilli = -1000;
    constexpr int32_t kMaximumPanMilli = 1000;
    if (levelMilliDb < kMinimumLevelMilliDb || levelMilliDb > kMaximumLevelMilliDb ||
        panMilli < kMinimumPanMilli || panMilli > kMaximumPanMilli) {
        return std::nullopt;
    }

    const float levelDb = static_cast<float>(levelMilliDb) / 1000.0F;
    if (panMilli == kMinimumPanMilli) {
        return SPro24DspStereoStripCoefficients{.left = Q214ForDb(levelDb), .right = 0};
    }
    if (panMilli == kMaximumPanMilli) {
        return SPro24DspStereoStripCoefficients{.left = 0, .right = Q214ForDb(levelDb)};
    }

    // ASFW policy, not recovered vendor behaviour: equal-power pan keeps the
    // sum of squared amplitudes constant across the gesture. Centre is
    // cos(pi/4) == sin(pi/4), approximately -3.0103 dB per side.
    constexpr float kHalfPi = 1.57079632679489661923F;
    const float normalized = static_cast<float>(panMilli - kMinimumPanMilli) /
        static_cast<float>(kMaximumPanMilli - kMinimumPanMilli);
    const float angle = normalized * kHalfPi;
    const float leftAmplitude = std::cos(angle);
    const float rightAmplitude = std::sin(angle);
    const float leftDb = levelDb + 20.0F * std::log10(leftAmplitude);
    const float rightDb = levelDb + 20.0F * std::log10(rightAmplitude);
    return SPro24DspStereoStripCoefficients{
        .left = Q214ForDb(leftDb),
        .right = Q214ForDb(rightDb),
    };
}

} // namespace ASFW::Audio::DICE::Focusrite
