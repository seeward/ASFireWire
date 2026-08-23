// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DICEExtensionState.hpp -- bounded, wire-accurate TCAT extension records.
//
// This is deliberately protocol data, not an app-facing control model.  The
// Saffire profile will translate these records into semantic strips/routes;
// register sections, block IDs, and coefficient packing never cross that
// boundary.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ASFW::Audio::DICE {

struct DiceRouterCaps final {
    bool exposed{false};
    bool readOnly{false};
    bool storable{false};
    uint16_t maximumEntryCount{0};
};

struct DiceMixerCaps final {
    bool exposed{false};
    bool readOnly{false};
    bool storable{false};
    uint8_t inputDeviceId{0};
    uint8_t outputDeviceId{0};
    uint8_t inputCount{0};
    uint8_t outputCount{0};
};

struct DiceExtensionGeneralCaps final {
    bool dynamicStreamFormat{false};
    bool storageAvailable{false};
    bool peakAvailable{false};
    uint8_t maximumTxStreams{0};
    uint8_t maximumRxStreams{0};
    bool streamFormatStorable{false};
    uint16_t asicType{0};
};

struct DiceExtensionCaps final {
    DiceRouterCaps router{};
    DiceMixerCaps mixer{};
    DiceExtensionGeneralCaps general{};

    static constexpr size_t kWireSize = 12;
};

/// A decoded TCAT router/peak record. The device uses the same four-byte
/// layout for router routes and peak records; `peak` is meaningful only for a
/// peak-section reply.
struct DiceRouterEntry final {
    uint8_t destinationBlock{0};
    uint8_t destinationChannel{0};
    uint8_t sourceBlock{0};
    uint8_t sourceChannel{0};
    uint16_t peak{0};

    static constexpr size_t kWireSize = 4;
};

/// TCAT allocates a fixed 16 x 18 coefficient window. The capability section
/// states how much of it a particular device exposes; never derive geometry
/// from an application profile alone.
inline constexpr uint8_t kDiceMaximumMixerInputs = 18;
inline constexpr uint8_t kDiceMaximumMixerOutputs = 16;
inline constexpr size_t kDiceMixerCoefficientWireBytes =
    size_t{4} * kDiceMaximumMixerInputs * kDiceMaximumMixerOutputs;

struct DiceMixerCoefficients final {
    uint8_t inputCount{0};
    uint8_t outputCount{0};
    std::array<uint16_t, size_t{kDiceMaximumMixerInputs} * kDiceMaximumMixerOutputs> values{};

    [[nodiscard]] constexpr uint16_t At(uint8_t output, uint8_t input) const noexcept {
        return values[size_t{output} * kDiceMaximumMixerInputs + input];
    }
};

/// Decode the caps section using the TCAT's big-endian quadlet layout.
[[nodiscard]] bool DecodeDiceExtensionCaps(std::span<const uint8_t> wire,
                                           DiceExtensionCaps& outCaps) noexcept;

/// Decode one router or peak record. Block/channel fields each occupy one
/// nibble pair, and the peak occupies the high half-word.
[[nodiscard]] bool DecodeDiceRouterEntry(std::span<const uint8_t> wire,
                                         DiceRouterEntry& outEntry) noexcept;

/// Decode the fixed mixer coefficient area, applying the device-discovered
/// active matrix dimensions. Unsupported dimensions fail closed.
[[nodiscard]] bool DecodeDiceMixerCoefficients(std::span<const uint8_t> wire,
                                                const DiceMixerCaps& caps,
                                                DiceMixerCoefficients& out) noexcept;

} // namespace ASFW::Audio::DICE
