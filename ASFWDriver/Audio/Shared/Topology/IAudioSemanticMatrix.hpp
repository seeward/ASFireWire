// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// IAudioSemanticMatrix.hpp -- bounded semantic matrix state.
//
// A mixer crosspoint matrix is operational state, not a small collection of
// scalar controls.  It therefore has a dedicated driver/UI seam instead of
// being squeezed into AudioControlSurface (which is intentionally bounded for
// device parameters) or the immutable topology graph.

#pragma once

#include "IAudioSemanticTopology.hpp"

#include <DriverKit/IOReturn.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>

namespace ASFW::Audio {

enum class AudioSemanticMatrixKind : uint32_t {
    Mixer = 1,
};

/// How mixer coefficients map to audible gain. A matrix snapshot carries this
/// explicitly so clients never relabel vendor integers as percentages.
enum class AudioSemanticMatrixGainLaw : uint16_t {
    None = 0,
    LinearNormalized = 1,
    /// DICE mixer coefficients are unsigned Q2.14 amplitudes: 0x4000 is
    /// unity (0 dB), zero is mute, and 0xffff is approximately +12 dB.
    UnsignedQ214Amplitude = 2,
};

/// Channel role within the UI-facing source group. A mono source is one
/// semantic channel and must never be presented as two fabricated L/R source
/// faders. Stereo members share a group ID and retain their individual
/// crosspoints. Destination pairing, where hardware supports it, is a
/// separate device-specific semantic.
enum class AudioSemanticMatrixChannelRole : uint8_t {
    None = 0,
    Mono = 1,
    Left = 2,
    Right = 3,
};

/// The job of a matrix output. Inputs always carry `None`; output roles keep
/// application-defined destinations (such as an effects send) out of the
/// generic monitor-mix selector.
enum class AudioSemanticMatrixOutputRole : uint8_t {
    None = 0,
    MonitorMix = 1,
    EffectSend = 2,
};

/// Product-owned meaning of one matrix cell. The coefficient array remains
/// complete hardware readback; this parallel map tells clients which cells
/// form a useful control and which must stay out of the console. In
/// particular, an effect return can be visible in a monitor bus while its
/// self-send cell is hidden on the effect-send bus.
enum class AudioSemanticMatrixCrosspointPresentation : uint8_t {
    Hidden = 0,
    ScalarReadback = 1,
    MonoLevelPan = 2,
    StereoLevelBalance = 3,
};

/// A single channel on one side of a semantic matrix. `portId` is opaque but
/// stable for a topology revision; it never encodes a DICE block/channel or a
/// vendor register address.
struct AudioSemanticMatrixAxis final {
    uint32_t portId{0};
    AudioSemanticSignalKind signalKind{AudioSemanticSignalKind::None};
    uint32_t signalIndex{0};
    /// Opaque semantic group identity. Never a DICE block/channel or register.
    uint32_t presentationGroupId{0};
    AudioSemanticMatrixChannelRole channelRole{AudioSemanticMatrixChannelRole::None};
    AudioSemanticMatrixOutputRole outputRole{AudioSemanticMatrixOutputRole::None};
    uint8_t _reserved[2]{};
};
static_assert(sizeof(AudioSemanticMatrixAxis) == 20);

/// Mute, solo, and the level a strip returns to when it stops being
/// suppressed. None of this is readable from the hardware: a mixer coefficient
/// of zero is what a mute *is*, so "muted at -6 dB" and "faded to silence" are
/// the same cell value. The driver therefore owns the distinction and publishes
/// it, or no client could render an un-mutable strip.
///
/// Records are sparse: a strip with no record is neither muted nor soloed and
/// its nominal level is simply its live coefficient. A record appears when a
/// strip is muted, soloed, or suppressed by another strip's solo on the same
/// bus -- the last case still needs its nominal remembered so that clearing the
/// solo can restore it.
struct AudioSemanticMatrixStripState final {
    uint32_t outputPresentationGroupId{0};
    uint32_t inputPresentationGroupId{0};
    /// Coefficients restored when the strip stops being suppressed. For a
    /// stereo strip these are its two diagonal cells; for a mono strip they are
    /// the one source cell against the destination's left and right rows.
    uint16_t nominalLeft{0};
    uint16_t nominalRight{0};
    /// Explicit user mute. Distinct from solo suppression, so clearing a solo
    /// never silently clears a mute the user set.
    uint8_t muted{0};
    uint8_t soloed{0};
    uint8_t reserved[2]{};
};
static_assert(sizeof(AudioSemanticMatrixStripState) == 16);

/// Bounded because the snapshot travels the inline structure-output path, which
/// caps at 4096 bytes and fails closed and silent above it. A suppression that
/// would need more records than this is refused rather than partially applied.
inline constexpr uint32_t kMaxAudioSemanticMatrixStripStates = 64;

inline constexpr uint32_t kAudioSemanticMatrixVersion = 6;
// Endpoint discovery is bounded independently from the dimensions of any one
// matrix. A client must enumerate only protocols which actually publish this
// semantic surface; configuration-capability discovery is a different API.
inline constexpr size_t kMaxAudioSemanticMatrixEndpoints = 8;
inline constexpr size_t kMaxAudioSemanticMatrixInputs = 24;
inline constexpr size_t kMaxAudioSemanticMatrixOutputs = 24;
inline constexpr size_t kMaxAudioSemanticMatrixCoefficients =
    kMaxAudioSemanticMatrixInputs * kMaxAudioSemanticMatrixOutputs;

/// Coefficients are unsigned native gain units. `gainLaw` and
/// `coefficientMaximum` declare their complete domain without exposing a
/// vendor register layout to the app.
struct AudioSemanticMatrixSnapshot final {
    uint32_t version{kAudioSemanticMatrixVersion};
    uint32_t deviceKind{0};
    uint64_t topologyRevision{0};
    uint32_t stateRevision{0};
    AudioSemanticMatrixKind kind{AudioSemanticMatrixKind::Mixer};
    uint32_t inputCount{0};
    uint32_t outputCount{0};
    uint16_t coefficientMaximum{0};
    AudioSemanticMatrixGainLaw gainLaw{AudioSemanticMatrixGainLaw::None};
    std::array<AudioSemanticMatrixAxis, kMaxAudioSemanticMatrixInputs> inputs{};
    std::array<AudioSemanticMatrixAxis, kMaxAudioSemanticMatrixOutputs> outputs{};
    std::array<uint16_t, kMaxAudioSemanticMatrixCoefficients> coefficients{};
    std::array<AudioSemanticMatrixCrosspointPresentation,
               kMaxAudioSemanticMatrixCoefficients> crosspointPresentations{};
    uint32_t stripStateCount{0};
    std::array<AudioSemanticMatrixStripState,
               kMaxAudioSemanticMatrixStripStates> stripStates{};

    [[nodiscard]] constexpr uint16_t Coefficient(uint32_t output,
                                                  uint32_t input) const noexcept {
        return coefficients[size_t{output} * kMaxAudioSemanticMatrixInputs + input];
    }
    [[nodiscard]] constexpr AudioSemanticMatrixCrosspointPresentation
    CrosspointPresentation(uint32_t output, uint32_t input) const noexcept {
        return crosspointPresentations[
            size_t{output} * kMaxAudioSemanticMatrixInputs + input];
    }

    /// The published state of one strip, or null when it carries none.
    [[nodiscard]] constexpr const AudioSemanticMatrixStripState* StripState(
        uint32_t outputPresentationGroupId,
        uint32_t inputPresentationGroupId) const noexcept {
        for (uint32_t index = 0; index < stripStateCount; ++index) {
            const auto& state = stripStates[index];
            if (state.outputPresentationGroupId == outputPresentationGroupId &&
                state.inputPresentationGroupId == inputPresentationGroupId) {
                return &state;
            }
        }
        return nullptr;
    }

    /// True when any strip on this bus is soloed, which is what suppresses the
    /// bus's other strips.
    [[nodiscard]] constexpr bool BusHasSolo(
        uint32_t outputPresentationGroupId) const noexcept {
        for (uint32_t index = 0; index < stripStateCount; ++index) {
            const auto& state = stripStates[index];
            if (state.outputPresentationGroupId == outputPresentationGroupId &&
                state.soloed != 0) {
                return true;
            }
        }
        return false;
    }

    /// A strip is silenced when the user muted it, or when some other strip on
    /// the same bus is soloed and this one is not.
    [[nodiscard]] constexpr bool StripIsSuppressed(
        uint32_t outputPresentationGroupId,
        uint32_t inputPresentationGroupId) const noexcept {
        const auto* state = StripState(outputPresentationGroupId,
                                       inputPresentationGroupId);
        if (state != nullptr && state->muted != 0) return true;
        if (!BusHasSolo(outputPresentationGroupId)) return false;
        return state == nullptr || state->soloed == 0;
    }
};
static_assert(sizeof(AudioSemanticMatrixSnapshot) == 3752);
static_assert(offsetof(AudioSemanticMatrixSnapshot, inputs) == 36);
static_assert(offsetof(AudioSemanticMatrixSnapshot, outputs) == 516);
static_assert(offsetof(AudioSemanticMatrixSnapshot, coefficients) == 996);
static_assert(offsetof(AudioSemanticMatrixSnapshot, crosspointPresentations) == 2148);
static_assert(offsetof(AudioSemanticMatrixSnapshot, stripStateCount) == 2724);
static_assert(offsetof(AudioSemanticMatrixSnapshot, stripStates) == 2728);

class IAudioSemanticMatrix {
public:
    using ApplyCallback = std::function<void(IOReturn)>;

    /// A device-declared grouped strip. These group IDs identify semantic
    /// source/destination objects in the current matrix snapshot; they are
    /// not vendor row, column, or register numbers. `levelMilliDb` is an
    /// absolute amplitude level. `balanceMilli` is interpreted as pan for a
    /// mono source and balance for a stereo source: -1000 (left), zero
    /// (centre), +1000 (right). The historical type name is retained because
    /// selector 1034 already shipped with it.
    struct StereoStripRequest final {
        uint32_t outputPresentationGroupId{0};
        uint32_t inputPresentationGroupId{0};
        int32_t levelMilliDb{0};
        int32_t balanceMilli{0};
    };

    virtual ~IAudioSemanticMatrix() = default;
    [[nodiscard]] virtual bool CopyAudioSemanticMatrix(
        AudioSemanticMatrixSnapshot& outSnapshot) const noexcept = 0;

    /// Writes exactly one semantic crosspoint. Implementations must treat the
    /// operation as a hardware transaction: establish current state, make one
    /// bounded mutation, then read the same hardware state back before
    /// updating their published snapshot.
    virtual void ApplyAudioSemanticMatrixCrosspoint(uint32_t outputPortId,
                                                     uint32_t inputPortId,
                                                     uint16_t coefficient,
                                                     ApplyCallback callback) {
        (void)outputPortId;
        (void)inputPortId;
        (void)coefficient;
        if (callback) callback(kIOReturnUnsupported);
    }

    /// Applies one grouped level/pan-or-balance gesture. Profiles must resolve
    /// both native coefficient cells, perform every required write, and
    /// publish only exact post-write readback. A generic matrix must refuse it.
    virtual void ApplyAudioSemanticMatrixStereoStrip(const StereoStripRequest& request,
                                                      ApplyCallback callback) {
        (void)request;
        if (callback) callback(kIOReturnUnsupported);
    }

    /// One absolute mute/solo gesture on a driver-verified strip. Absolute
    /// rather than a toggle, so a repeated or racing request is idempotent and
    /// two clients cannot invert each other's view of the strip.
    struct StripSuppressionRequest final {
        uint32_t outputPresentationGroupId{0};
        uint32_t inputPresentationGroupId{0};
        bool muted{false};
        bool soloed{false};
    };

    /// Applies one mute/solo gesture. Mute silences a single strip; solo
    /// silences every other strip on the same bus, so a profile must write
    /// every cell whose effective value changes and confirm them all by
    /// readback. Neither is a hardware bit: both are coefficient policy over
    /// remembered nominal levels. A generic matrix must refuse it.
    virtual void ApplyAudioSemanticMatrixStripSuppression(
        const StripSuppressionRequest& request, ApplyCallback callback) {
        (void)request;
        if (callback) callback(kIOReturnUnsupported);
    }
};

enum class AudioSemanticMatrixValidationError : uint32_t {
    InvalidVersion,
    MissingDeviceKind,
    MissingTopologyRevision,
    InvalidKind,
    CountOutOfRange,
    MissingCoefficientDomain,
    InvalidGainLaw,
    CoefficientOutOfRange,
    InvalidCrosspointPresentation,
    StripStateCountOutOfRange,
    InvalidStripState,
    DuplicateStripState,
    InvalidInput,
    DuplicateInputPort,
    InvalidOutput,
    DuplicateOutputPort,
};

namespace Detail {

[[nodiscard]] constexpr bool IsValid(AudioSemanticMatrixKind kind) noexcept {
    return kind == AudioSemanticMatrixKind::Mixer;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticMatrixGainLaw gainLaw) noexcept {
    return gainLaw == AudioSemanticMatrixGainLaw::LinearNormalized ||
           gainLaw == AudioSemanticMatrixGainLaw::UnsignedQ214Amplitude;
}

[[nodiscard]] constexpr bool IsValid(const AudioSemanticMatrixAxis& axis) noexcept {
    return axis.portId != 0 && axis.signalKind != AudioSemanticSignalKind::None &&
           axis.presentationGroupId != 0 &&
           (axis.channelRole == AudioSemanticMatrixChannelRole::Mono ||
            axis.channelRole == AudioSemanticMatrixChannelRole::Left ||
            axis.channelRole == AudioSemanticMatrixChannelRole::Right);
}

[[nodiscard]] constexpr bool IsValid(
    AudioSemanticMatrixCrosspointPresentation presentation) noexcept {
    return presentation == AudioSemanticMatrixCrosspointPresentation::Hidden ||
           presentation == AudioSemanticMatrixCrosspointPresentation::ScalarReadback ||
           presentation == AudioSemanticMatrixCrosspointPresentation::MonoLevelPan ||
           presentation == AudioSemanticMatrixCrosspointPresentation::StereoLevelBalance;
}

[[nodiscard]] constexpr bool IsValidInput(const AudioSemanticMatrixAxis& axis) noexcept {
    return IsValid(axis) && axis.outputRole == AudioSemanticMatrixOutputRole::None;
}

[[nodiscard]] constexpr bool IsValidOutput(const AudioSemanticMatrixAxis& axis) noexcept {
    return IsValid(axis) &&
           (axis.outputRole == AudioSemanticMatrixOutputRole::MonitorMix ||
            axis.outputRole == AudioSemanticMatrixOutputRole::EffectSend);
}

} // namespace Detail

[[nodiscard]] constexpr std::expected<void, AudioSemanticMatrixValidationError>
ValidateAudioSemanticMatrix(const AudioSemanticMatrixSnapshot& snapshot) noexcept {
    using Error = AudioSemanticMatrixValidationError;
    if (snapshot.version != kAudioSemanticMatrixVersion) {
        return std::unexpected(Error::InvalidVersion);
    }
    if (snapshot.deviceKind == 0) return std::unexpected(Error::MissingDeviceKind);
    if (snapshot.topologyRevision == 0) return std::unexpected(Error::MissingTopologyRevision);
    if (!Detail::IsValid(snapshot.kind)) return std::unexpected(Error::InvalidKind);
    if (snapshot.inputCount == 0 || snapshot.outputCount == 0 ||
        snapshot.inputCount > kMaxAudioSemanticMatrixInputs ||
        snapshot.outputCount > kMaxAudioSemanticMatrixOutputs) {
        return std::unexpected(Error::CountOutOfRange);
    }
    if (snapshot.coefficientMaximum == 0) {
        return std::unexpected(Error::MissingCoefficientDomain);
    }
    if (!Detail::IsValid(snapshot.gainLaw)) return std::unexpected(Error::InvalidGainLaw);
    for (uint32_t input = 0; input < snapshot.inputCount; ++input) {
        if (!Detail::IsValidInput(snapshot.inputs[input])) return std::unexpected(Error::InvalidInput);
        for (uint32_t earlier = 0; earlier < input; ++earlier) {
            if (snapshot.inputs[earlier].portId == snapshot.inputs[input].portId) {
                return std::unexpected(Error::DuplicateInputPort);
            }
        }
    }
    for (uint32_t output = 0; output < snapshot.outputCount; ++output) {
        if (!Detail::IsValidOutput(snapshot.outputs[output])) {
            return std::unexpected(Error::InvalidOutput);
        }
        for (uint32_t earlier = 0; earlier < output; ++earlier) {
            if (snapshot.outputs[earlier].portId == snapshot.outputs[output].portId) {
                return std::unexpected(Error::DuplicateOutputPort);
            }
        }
    }
    for (uint32_t output = 0; output < snapshot.outputCount; ++output) {
        for (uint32_t input = 0; input < snapshot.inputCount; ++input) {
            if (snapshot.Coefficient(output, input) > snapshot.coefficientMaximum) {
                return std::unexpected(Error::CoefficientOutOfRange);
            }
            if (!Detail::IsValid(snapshot.CrosspointPresentation(output, input))) {
                return std::unexpected(Error::InvalidCrosspointPresentation);
            }
        }
    }
    if (snapshot.stripStateCount > kMaxAudioSemanticMatrixStripStates) {
        return std::unexpected(Error::StripStateCountOutOfRange);
    }
    for (uint32_t index = 0; index < snapshot.stripStateCount; ++index) {
        const auto& state = snapshot.stripStates[index];
        // A record that is neither muted nor soloed still exists to hold a
        // nominal for a solo-suppressed strip, so only the identity and the
        // coefficient domain are constrained here.
        if (state.outputPresentationGroupId == 0 ||
            state.inputPresentationGroupId == 0 ||
            state.muted > 1 || state.soloed > 1 ||
            state.nominalLeft > snapshot.coefficientMaximum ||
            state.nominalRight > snapshot.coefficientMaximum) {
            return std::unexpected(Error::InvalidStripState);
        }
        for (uint32_t other = 0; other < index; ++other) {
            if (snapshot.stripStates[other].outputPresentationGroupId ==
                    state.outputPresentationGroupId &&
                snapshot.stripStates[other].inputPresentationGroupId ==
                    state.inputPresentationGroupId) {
                return std::unexpected(Error::DuplicateStripState);
            }
        }
    }
    return {};
}

} // namespace ASFW::Audio
