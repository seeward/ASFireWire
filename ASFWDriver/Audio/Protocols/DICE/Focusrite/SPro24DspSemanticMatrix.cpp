// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSemanticMatrix.hpp"

#include "../Core/DICERouterMixerTopology.hpp"

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

// ---------------------------------------------------------------------------
// Vendor input signal table
//
// Decoded from MixControl's `Pro24DSP_IpSigTab`. Every vendor entry names one
// signal and carries a separate (block, channel) per rate mode, because two
// pairs move router channel with rate: `FX(Anlg 1/2)` is Ins0:8/9 at 1x but
// Ins0:4/5 at 2x, and `FmRvb 0/1` is Ins0:14/15 then Ins0:6/7. Deriving a
// channel number arithmetically instead of consulting the table silently
// mislabels both pairs above 48 kHz -- and on the output side the same shift
// swaps a line output with a channel-strip send. See documentation/SPRO24DSP.md
// sections 5.0 and 5.0.1.
//
// Source-block IDs are TCAT vocabulary, cross-validated with the local ALSA
// control reference: Aes=0, Adat=1, Mixer=2, Ins0=4, Ins1=5, ArmAprAudio=10,
// Avs0=11, Avs1=12, Mute=15 --
// references/alsa-userspace-control-protocols-impl/protocols/dice/src/tcat/
// extension/router_entry.rs:81-95.
// ---------------------------------------------------------------------------

using Kind = AudioSemanticSignalKind;

constexpr uint8_t kBlockAes = 0;
constexpr uint8_t kBlockAdat = 1;
constexpr uint8_t kBlockMixer = 2;
constexpr uint8_t kBlockIns0 = 4;
constexpr uint8_t kBlockArmApr = 10;
constexpr uint8_t kBlockAvs0 = 11;
constexpr uint8_t kBlockMute = 15;

// A router block field is four bits wide, so this can never match a real
// route and an absent signal is unreachable by construction.
constexpr uint8_t kSignalAbsent = 0xff;
constexpr size_t kRateModeCount = 3;

// Several distinct vendor categories collapse onto `Auxiliary`, which carries
// no sub-kind, so their signal indices must not overlap. Only the DSP returns
// are reachable in any measured router image; the rest are numbered so that an
// unexpected route is merely unnamed rather than reported as another signal.
constexpr uint32_t kAuxEffectReturnFirst = 1;   // FX(Anlg 1/2)
constexpr uint32_t kAuxReverbReturnFirst = 3;   // FmRvb 0/1
constexpr uint32_t kAuxMixReturnFirst = 5;      // FromMix1..8
constexpr uint32_t kAuxReverbSendFirst = 13;    // RvbSend-1/2
constexpr uint32_t kAuxArmFirst = 15;           // FromArm-0/1
constexpr uint32_t kAuxMuted = 17;              // Off

struct SPro24InputSignal final {
    Kind kind;
    /// One-based, user-facing, and disjoint within `kind`.
    uint32_t signalIndex;
    /// Set only where the vendor table pairs two entries as one stereo source.
    /// Never inferred from adjacent numbering.
    bool stereoPair;
    /// Router coordinates indexed by DiceRateMode.
    std::array<uint8_t, kRateModeCount> block;
    std::array<uint8_t, kRateModeCount> channel;
};

// The Pro 24 DSP publishes 44.1/48/88.2/96 kHz only, so no entry exists at the
// High mode. That column is left absent rather than assumed to repeat the low
// one: a High-rate lookup finds nothing and falls back to an unnamed source.
[[nodiscard]] constexpr SPro24InputSignal SameAtBothRates(
    Kind kind, uint32_t index, bool stereoPair, uint8_t block, uint8_t channel) noexcept {
    return {kind, index, stereoPair,
            {block, block, kSignalAbsent},
            {channel, channel, kSignalAbsent}};
}

[[nodiscard]] constexpr SPro24InputSignal MovesAtMidRate(
    Kind kind, uint32_t index, bool stereoPair, uint8_t block,
    uint8_t lowChannel, uint8_t midChannel) noexcept {
    return {kind, index, stereoPair,
            {block, block, kSignalAbsent},
            {lowChannel, midChannel, kSignalAbsent}};
}

[[nodiscard]] constexpr SPro24InputSignal LowRateOnly(
    Kind kind, uint32_t index, bool stereoPair, uint8_t block, uint8_t channel) noexcept {
    return {kind, index, stereoPair,
            {block, kSignalAbsent, kSignalAbsent},
            {channel, kSignalAbsent, kSignalAbsent}};
}

// All 41 entries of Pro24DSP_IpSigTab, in vendor order.
constexpr auto kSPro24InputSignals = std::to_array<SPro24InputSignal>({
    // Anlg In 1..4. Vendor numbering is not router channel order: the rear
    // pair is 3/4. Corroborated by the fixed meter ordering (Ins0 2,3,0,1).
    SameAtBothRates(Kind::AnalogLine, 1, false, kBlockIns0, 2),
    SameAtBothRates(Kind::AnalogLine, 2, false, kBlockIns0, 3),
    SameAtBothRates(Kind::AnalogLine, 3, false, kBlockIns0, 0),
    SameAtBothRates(Kind::AnalogLine, 4, false, kBlockIns0, 1),
    // SPDIF 1/2 -- the coax pair, exposed by the vendor at Aes:6/7.
    SameAtBothRates(Kind::DigitalSpdif, 1, true, kBlockAes, 6),
    SameAtBothRates(Kind::DigitalSpdif, 2, true, kBlockAes, 7),
    // ADAT In 1..8. Channels 5..8 exist at 1x only.
    SameAtBothRates(Kind::DigitalAdat, 1, false, kBlockAdat, 0),
    SameAtBothRates(Kind::DigitalAdat, 2, false, kBlockAdat, 1),
    SameAtBothRates(Kind::DigitalAdat, 3, false, kBlockAdat, 2),
    SameAtBothRates(Kind::DigitalAdat, 4, false, kBlockAdat, 3),
    LowRateOnly(Kind::DigitalAdat, 5, false, kBlockAdat, 4),
    LowRateOnly(Kind::DigitalAdat, 6, false, kBlockAdat, 5),
    LowRateOnly(Kind::DigitalAdat, 7, false, kBlockAdat, 6),
    LowRateOnly(Kind::DigitalAdat, 8, false, kBlockAdat, 7),
    // DAW 1..8 -- host playback, four stereo pairs.
    SameAtBothRates(Kind::HostStream, 1, true, kBlockAvs0, 0),
    SameAtBothRates(Kind::HostStream, 2, true, kBlockAvs0, 1),
    SameAtBothRates(Kind::HostStream, 3, true, kBlockAvs0, 2),
    SameAtBothRates(Kind::HostStream, 4, true, kBlockAvs0, 3),
    SameAtBothRates(Kind::HostStream, 5, true, kBlockAvs0, 4),
    SameAtBothRates(Kind::HostStream, 6, true, kBlockAvs0, 5),
    SameAtBothRates(Kind::HostStream, 7, true, kBlockAvs0, 6),
    SameAtBothRates(Kind::HostStream, 8, true, kBlockAvs0, 7),
    // FromMix1..8 -- monitor bus returns.
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 0, false, kBlockMixer, 0),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 1, false, kBlockMixer, 1),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 2, false, kBlockMixer, 2),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 3, false, kBlockMixer, 3),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 4, false, kBlockMixer, 4),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 5, false, kBlockMixer, 5),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 6, false, kBlockMixer, 6),
    SameAtBothRates(Kind::Auxiliary, kAuxMixReturnFirst + 7, false, kBlockMixer, 7),
    // RvbSend-1/2 -- the vendor's own name for mixer rows 8/9.
    SameAtBothRates(Kind::Auxiliary, kAuxReverbSendFirst + 0, false, kBlockMixer, 8),
    SameAtBothRates(Kind::Auxiliary, kAuxReverbSendFirst + 1, false, kBlockMixer, 9),
    // SPDIF 3/4 -- a second vendor pair at Aes:4/5, numbered 3/4 by the table
    // and not by its router offset.
    SameAtBothRates(Kind::DigitalSpdif, 3, true, kBlockAes, 4),
    SameAtBothRates(Kind::DigitalSpdif, 4, true, kBlockAes, 5),
    // FX(Anlg 1/2) -- channel-strip returns. Two mono strips, not a pair.
    MovesAtMidRate(Kind::Auxiliary, kAuxEffectReturnFirst + 0, false, kBlockIns0, 8, 4),
    MovesAtMidRate(Kind::Auxiliary, kAuxEffectReturnFirst + 1, false, kBlockIns0, 9, 5),
    // FmRvb 0/1 -- the stereo reverb return.
    MovesAtMidRate(Kind::Auxiliary, kAuxReverbReturnFirst + 0, true, kBlockIns0, 14, 6),
    MovesAtMidRate(Kind::Auxiliary, kAuxReverbReturnFirst + 1, true, kBlockIns0, 15, 7),
    // FromArm-0/1 -- ARM/APR audio.
    SameAtBothRates(Kind::Auxiliary, kAuxArmFirst + 0, false, kBlockArmApr, 0),
    SameAtBothRates(Kind::Auxiliary, kAuxArmFirst + 1, false, kBlockArmApr, 1),
    // Off -- a mixer input deliberately wired to silence.
    SameAtBothRates(Kind::Auxiliary, kAuxMuted, false, kBlockMute, 0),
});

static_assert(kSPro24InputSignals.size() == 41,
              "Pro24DSP_IpSigTab has 41 entries; a short table silently unnames a source.");

/// The vendor entry naming `route`'s source at `rateMode`, or null when the
/// active router points at something the table does not describe.
[[nodiscard]] const SPro24InputSignal* FindSPro24InputSignal(const DiceRouterEntry& route,
                                                              DiceRateMode rateMode) noexcept {
    const auto rate = static_cast<size_t>(rateMode);
    if (rate >= kRateModeCount) return nullptr;
    for (const auto& signal : kSPro24InputSignals) {
        if (signal.block[rate] == route.sourceBlock &&
            signal.channel[rate] == route.sourceChannel) {
            return &signal;
        }
    }
    return nullptr;
}

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
           (axis.signalIndex == kAuxReverbReturnFirst ||
            axis.signalIndex == kAuxReverbReturnFirst + 1U);
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
            ? FindSPro24InputSignal(binding.route, rateMode) : nullptr;
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

std::optional<SPro24DspStereoStripLayout>
ResolveSPro24DspStereoStrip(const DiceMixerCoefficients& coefficients,
                            const DiceRouterEntries& routes,
                            DiceRateMode rateMode,
                            uint32_t outputPresentationGroupId,
                            uint32_t inputPresentationGroupId) noexcept {
    AudioSemanticMatrixSnapshot snapshot{};
    if (!BuildSPro24DspSemanticMatrix(coefficients, routes, rateMode, snapshot)) {
        return std::nullopt;
    }

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
                          DiceRateMode rateMode,
                          uint32_t outputPresentationGroupId,
                          uint32_t inputPresentationGroupId) noexcept {
    AudioSemanticMatrixSnapshot snapshot{};
    if (!BuildSPro24DspSemanticMatrix(coefficients, routes, rateMode, snapshot)) {
        return std::nullopt;
    }

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
