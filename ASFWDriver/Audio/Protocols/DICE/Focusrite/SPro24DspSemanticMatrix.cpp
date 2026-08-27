// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSemanticMatrix.hpp"

#include "../Core/DICERouterMixerTopology.hpp"

#include <algorithm>
#include <cmath>

namespace ASFW::Audio::DICE::Focusrite {

namespace {

constexpr uint32_t kMixerInputPortBase = 0x5352'0000;
constexpr uint32_t kMixerOutputPortBase = 0x5353'0000;
constexpr uint32_t kMixerInputPresentationGroupBase = 0x5354'0000;
constexpr uint32_t kMixerOutputPresentationGroupBase = 0x5355'0000;

// Product presentation is applied only after the generic TCAT layer has
// joined the active router and anonymous 18 x 16 coefficient window.

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

AudioSemanticSignalKind SignalKindForSPro24Source(const DiceRouterEntry& entry) noexcept {
    // SPro24 DSP's Ins0 block has profile-defined sub-ranges. Keep the DSP
    // returns out of the physical-line presentation even though they share a
    // TCAT source block. Cross-validated with the local
    // snd-firewire-ctl-services spro24dsp Tcd22xx specification.
    if (entry.sourceBlock == 4) {
        // All four analog inputs are one vendor category, "Anlg In 1..4".
        // Do NOT split channels 2/3 out as a microphone kind: whether inputs
        // 1/2 are presenting a preamp is an input *mode*, published separately
        // by the physical-input surface, not the identity of the signal. A
        // strip labelled MIC while its jack is switched to line is a lie.
        if (entry.sourceChannel < 4) return AudioSemanticSignalKind::AnalogLine;
        return AudioSemanticSignalKind::Auxiliary; // channel-strip/reverb return
    }
    return SignalKindForSource(entry.sourceBlock);
}

uint32_t SignalIndexForSource(const DiceRouterEntry& entry) noexcept {
    // Signal indices are one-based user-facing channel numbers. Distinct TCAT
    // source blocks of one signal kind need disjoint identities as well.
    switch (entry.sourceBlock) {
    case 0:
        // The Pro 24 DSP exposes its coax S/PDIF pair at AES 6/7; keep the
        // user-facing pair numbered 1/2 rather than leaking router offsets.
        return entry.sourceChannel >= 6 ? uint32_t{entry.sourceChannel} - 5U
                                        : uint32_t{entry.sourceChannel} + 1U;
    case 4:
        // Vendor analog-input numbering is NOT the router channel order:
        // Ins0:2 -> Anlg In 1, Ins0:3 -> 2, Ins0:0 -> 3, Ins0:1 -> 4. The rear
        // pair is 3/4, not 1/2. Recovered from MixControl's Pro24DSP_IpSigTab,
        // and corroborated by the FIXED meter ordering (Ins0 2,3,0,1).
        if (entry.sourceChannel < 2) return uint32_t{entry.sourceChannel} + 3U;
        if (entry.sourceChannel < 4) return uint32_t{entry.sourceChannel} - 1U;
        if (entry.sourceChannel >= 8 && entry.sourceChannel < 10) {
            return uint32_t{entry.sourceChannel} - 7U; // channel strip 1/2
        }
        if (entry.sourceChannel >= 14) return uint32_t{entry.sourceChannel} - 11U; // reverb 1/2
        return uint32_t{entry.sourceChannel} + 1U;
    case 5: return uint32_t{entry.sourceChannel} + 17U;
    case 11: return uint32_t{entry.sourceChannel} + 1U;
    case 12: return uint32_t{entry.sourceChannel} + 17U;
    default: return uint32_t{entry.sourceChannel} + 1U;
    }
}

struct InputPresentation final {
    uint32_t groupId{0};
    AudioSemanticMatrixChannelRole role{AudioSemanticMatrixChannelRole::Mono};
};

InputPresentation PresentationForSource(const DiceRouterEntry& entry,
                                        AudioSemanticSignalKind kind,
                                        uint32_t signalIndex,
                                        uint32_t input) noexcept {
    // Only pairs verified as one hardware/stereo source receive L/R roles.
    // ADAT and individual analog/strip sources remain mono, even if their
    // labels are numerically adjacent: pairing them from index coincidence is
    // exactly the UI bug this semantic field avoids.
    const bool stereoPair = kind == AudioSemanticSignalKind::HostStream ||
        kind == AudioSemanticSignalKind::DigitalSpdif ||
        (kind == AudioSemanticSignalKind::Auxiliary && entry.sourceBlock == 4 &&
         entry.sourceChannel >= 14 && entry.sourceChannel < 16);
    if (!stereoPair) {
        return {.groupId = kMixerInputPresentationGroupBase + input + 1U,
                .role = AudioSemanticMatrixChannelRole::Mono};
    }

    const uint32_t pairStart = (signalIndex - 1U) & ~uint32_t{1};
    const uint32_t kindOffset = static_cast<uint32_t>(kind) << 8U;
    return {.groupId = kMixerInputPresentationGroupBase + kindOffset + pairStart + 1U,
            .role = (signalIndex & 1U) != 0U ? AudioSemanticMatrixChannelRole::Left
                                              : AudioSemanticMatrixChannelRole::Right};
}

[[nodiscard]] bool IsSPro24ReverbReturn(const DiceMixerInputBinding& binding) noexcept {
    return binding.routed && binding.route.sourceBlock == 4U &&
           binding.route.sourceChannel >= 14U && binding.route.sourceChannel < 16U;
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
        const auto& route = binding.route;
        const bool found = binding.routed;
        const auto kind = found ? SignalKindForSPro24Source(route)
                                : AudioSemanticSignalKind::Auxiliary;
        const uint32_t signalIndex = found ? SignalIndexForSource(route) : input + 1U;
        const auto presentation = found
            ? PresentationForSource(route, kind, signalIndex, input)
            : InputPresentation{.groupId = kMixerInputPresentationGroupBase + input + 1U,
                                .role = AudioSemanticMatrixChannelRole::Mono};
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
                IsSPro24ReverbReturn(topology.inputs[input])) {
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

std::optional<SPro24DspStereoStripLayout>
ResolveSPro24DspStereoStrip(const DiceMixerCoefficients& coefficients,
                            const DiceRouterEntries& routes,
                            uint32_t outputPresentationGroupId,
                            uint32_t inputPresentationGroupId) noexcept {
    AudioSemanticMatrixSnapshot snapshot{};
    if (!BuildSPro24DspSemanticMatrix(coefficients, routes, snapshot)) return std::nullopt;

    std::optional<uint8_t> inputLeft;
    std::optional<uint8_t> inputRight;
    std::optional<uint8_t> outputLeft;
    std::optional<uint8_t> outputRight;
    std::optional<uint32_t> semanticInputLeft;
    std::optional<uint32_t> semanticInputRight;
    std::optional<uint32_t> semanticOutputLeft;
    std::optional<uint32_t> semanticOutputRight;
    for (uint32_t input = 0; input < snapshot.inputCount; ++input) {
        const auto& axis = snapshot.inputs[input];
        if (axis.presentationGroupId != inputPresentationGroupId) continue;
        const auto rawInput = RawInputForPortId(axis.portId);
        if (!rawInput) return std::nullopt;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left && !inputLeft) {
            inputLeft = *rawInput;
            semanticInputLeft = input;
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right && !inputRight) {
            inputRight = *rawInput;
            semanticInputRight = input;
        } else {
            return std::nullopt;
        }
    }
    for (uint32_t output = 0; output < snapshot.outputCount; ++output) {
        const auto& axis = snapshot.outputs[output];
        if (axis.presentationGroupId != outputPresentationGroupId) continue;
        const auto rawOutput = RawOutputForPortId(axis.portId);
        if (!rawOutput) return std::nullopt;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left && !outputLeft) {
            outputLeft = *rawOutput;
            semanticOutputLeft = output;
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right && !outputRight) {
            outputRight = *rawOutput;
            semanticOutputRight = output;
        } else {
            return std::nullopt;
        }
    }
    if (!inputLeft || !inputRight || !outputLeft || !outputRight ||
        !semanticInputLeft || !semanticInputRight ||
        !semanticOutputLeft || !semanticOutputRight ||
        *inputRight != static_cast<uint8_t>(*inputLeft + 1U) ||
        *outputRight != static_cast<uint8_t>(*outputLeft + 1U) ||
        snapshot.CrosspointPresentation(*semanticOutputLeft, *semanticInputLeft) !=
            AudioSemanticMatrixCrosspointPresentation::StereoLevelBalance ||
        snapshot.CrosspointPresentation(*semanticOutputRight, *semanticInputRight) !=
            AudioSemanticMatrixCrosspointPresentation::StereoLevelBalance) {
        return std::nullopt;
    }
    return SPro24DspStereoStripLayout{
        .inputLeft = *inputLeft,
        .inputRight = *inputRight,
        .outputLeft = *outputLeft,
        .outputRight = *outputRight,
    };
}

std::optional<SPro24DspMonoStripLayout>
ResolveSPro24DspMonoStrip(const DiceMixerCoefficients& coefficients,
                          const DiceRouterEntries& routes,
                          uint32_t outputPresentationGroupId,
                          uint32_t inputPresentationGroupId) noexcept {
    AudioSemanticMatrixSnapshot snapshot{};
    if (!BuildSPro24DspSemanticMatrix(coefficients, routes, snapshot)) return std::nullopt;

    std::optional<uint8_t> input;
    std::optional<uint8_t> outputLeft;
    std::optional<uint8_t> outputRight;
    std::optional<uint32_t> semanticInput;
    std::optional<uint32_t> semanticOutputLeft;
    std::optional<uint32_t> semanticOutputRight;

    for (uint32_t index = 0; index < snapshot.inputCount; ++index) {
        const auto& axis = snapshot.inputs[index];
        if (axis.presentationGroupId != inputPresentationGroupId) continue;
        if (input || axis.channelRole != AudioSemanticMatrixChannelRole::Mono) {
            return std::nullopt;
        }
        input = RawInputForPortId(axis.portId);
        semanticInput = index;
    }
    for (uint32_t index = 0; index < snapshot.outputCount; ++index) {
        const auto& axis = snapshot.outputs[index];
        if (axis.presentationGroupId != outputPresentationGroupId) continue;
        const auto rawOutput = RawOutputForPortId(axis.portId);
        if (!rawOutput) return std::nullopt;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left && !outputLeft) {
            outputLeft = *rawOutput;
            semanticOutputLeft = index;
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right && !outputRight) {
            outputRight = *rawOutput;
            semanticOutputRight = index;
        } else {
            return std::nullopt;
        }
    }
    if (!input || !outputLeft || !outputRight || !semanticInput ||
        !semanticOutputLeft || !semanticOutputRight ||
        *outputRight != static_cast<uint8_t>(*outputLeft + 1U) ||
        snapshot.CrosspointPresentation(*semanticOutputLeft, *semanticInput) !=
            AudioSemanticMatrixCrosspointPresentation::MonoLevelPan ||
        snapshot.CrosspointPresentation(*semanticOutputRight, *semanticInput) !=
            AudioSemanticMatrixCrosspointPresentation::MonoLevelPan) {
        return std::nullopt;
    }
    return SPro24DspMonoStripLayout{
        .input = *input,
        .outputLeft = *outputLeft,
        .outputRight = *outputRight,
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
