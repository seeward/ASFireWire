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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

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

/// A single channel on one side of a semantic matrix. `portId` is opaque but
/// stable for a topology revision; it never encodes a DICE block/channel or a
/// vendor register address.
struct AudioSemanticMatrixAxis final {
    uint32_t portId{0};
    AudioSemanticSignalKind signalKind{AudioSemanticSignalKind::None};
    uint32_t signalIndex{0};
};
static_assert(sizeof(AudioSemanticMatrixAxis) == 12);

inline constexpr uint32_t kAudioSemanticMatrixVersion = 2;
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

    [[nodiscard]] constexpr uint16_t Coefficient(uint32_t output,
                                                  uint32_t input) const noexcept {
        return coefficients[size_t{output} * kMaxAudioSemanticMatrixInputs + input];
    }
};
static_assert(sizeof(AudioSemanticMatrixSnapshot) == 1768);
static_assert(offsetof(AudioSemanticMatrixSnapshot, inputs) == 36);
static_assert(offsetof(AudioSemanticMatrixSnapshot, outputs) == 324);
static_assert(offsetof(AudioSemanticMatrixSnapshot, coefficients) == 612);

class IAudioSemanticMatrix {
public:
    virtual ~IAudioSemanticMatrix() = default;
    [[nodiscard]] virtual bool CopyAudioSemanticMatrix(
        AudioSemanticMatrixSnapshot& outSnapshot) const noexcept = 0;
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
    return axis.portId != 0 && axis.signalKind != AudioSemanticSignalKind::None;
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
        if (!Detail::IsValid(snapshot.inputs[input])) return std::unexpected(Error::InvalidInput);
        for (uint32_t earlier = 0; earlier < input; ++earlier) {
            if (snapshot.inputs[earlier].portId == snapshot.inputs[input].portId) {
                return std::unexpected(Error::DuplicateInputPort);
            }
        }
    }
    for (uint32_t output = 0; output < snapshot.outputCount; ++output) {
        if (!Detail::IsValid(snapshot.outputs[output])) return std::unexpected(Error::InvalidOutput);
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
        }
    }
    return {};
}

} // namespace ASFW::Audio
