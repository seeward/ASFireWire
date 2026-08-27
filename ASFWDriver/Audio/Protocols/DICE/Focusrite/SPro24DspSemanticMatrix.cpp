// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSemanticMatrix.hpp"

#include <algorithm>
#include <cmath>

namespace ASFW::Audio::DICE::Focusrite {

namespace {

constexpr uint32_t kMixerInputPortBase = 0x5352'0000;
constexpr uint32_t kMixerOutputPortBase = 0x5353'0000;
constexpr uint32_t kMixerInputPresentationGroupBase = 0x5354'0000;
constexpr uint32_t kMixerOutputPresentationGroupBase = 0x5355'0000;

// The Pro 24 DSP has a fixed 18 x 16 DICE mixer. Rows are physically paired
// as mixer output 1/2 through 15/16. This describes the hardware graph only;
// it does not claim MixControl's optional linked-level/pan presentation.
// Cross-validated with the local ALSA control service's
// protocols/dice/src/focusrite/spro24dsp.rs signal-flow diagram, lines 47-70.

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

    outSnapshot = {};
    outSnapshot.deviceKind = kSPro24DspSemanticDeviceKind;
    // The coordinator substitutes the endpoint's live revision at publication.
    outSnapshot.topologyRevision = 1;
    outSnapshot.kind = AudioSemanticMatrixKind::Mixer;
    outSnapshot.inputCount = coefficients.inputCount;
    outSnapshot.outputCount = coefficients.outputCount;
    outSnapshot.coefficientMaximum = 65535;
    outSnapshot.gainLaw = AudioSemanticMatrixGainLaw::UnsignedQ214Amplitude;

    for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
        DiceRouterEntry route{};
        const bool found = FindMixerInputRoute(routes, input, route);
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
    for (uint32_t output = 0; output < outSnapshot.outputCount; ++output) {
        const uint32_t rawOutput = output;
        outSnapshot.outputs[output] = {
            .portId = kMixerOutputPortBase + rawOutput + 1U,
            .signalKind = AudioSemanticSignalKind::Auxiliary,
            .signalIndex = rawOutput + 1U,
            .presentationGroupId = kMixerOutputPresentationGroupBase +
                ((rawOutput & ~uint32_t{1}) + 1U),
            .channelRole = (rawOutput & 1U) == 0U ? AudioSemanticMatrixChannelRole::Left
                                                   : AudioSemanticMatrixChannelRole::Right,
            .outputRole = AudioSemanticMatrixOutputRole::MonitorMix,
        };
        for (uint32_t input = 0; input < coefficients.inputCount; ++input) {
            outSnapshot.coefficients[size_t{output} * kMaxAudioSemanticMatrixInputs + input] =
                coefficients.At(static_cast<uint8_t>(rawOutput), static_cast<uint8_t>(input));
        }
    }
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
    for (uint32_t input = 0; input < snapshot.inputCount; ++input) {
        const auto& axis = snapshot.inputs[input];
        if (axis.presentationGroupId != inputPresentationGroupId) continue;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left && !inputLeft) {
            inputLeft = static_cast<uint8_t>(input);
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right && !inputRight) {
            inputRight = static_cast<uint8_t>(input);
        } else {
            return std::nullopt;
        }
    }
    for (uint32_t output = 0; output < snapshot.outputCount; ++output) {
        const auto& axis = snapshot.outputs[output];
        if (axis.presentationGroupId != outputPresentationGroupId) continue;
        if (axis.channelRole == AudioSemanticMatrixChannelRole::Left && !outputLeft) {
            outputLeft = static_cast<uint8_t>(output);
        } else if (axis.channelRole == AudioSemanticMatrixChannelRole::Right && !outputRight) {
            outputRight = static_cast<uint8_t>(output);
        } else {
            return std::nullopt;
        }
    }
    if (!inputLeft || !inputRight || !outputLeft || !outputRight ||
        *inputRight != static_cast<uint8_t>(*inputLeft + 1U) ||
        *outputRight != static_cast<uint8_t>(*outputLeft + 1U)) {
        return std::nullopt;
    }
    return SPro24DspStereoStripLayout{
        .inputLeft = *inputLeft,
        .inputRight = *inputRight,
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

} // namespace ASFW::Audio::DICE::Focusrite
