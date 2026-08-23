// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "DICEExtensionState.hpp"

#include "../../../../Common/WireFormat.hpp"

namespace ASFW::Audio::DICE {

namespace {

constexpr uint32_t kExposed = 1U << 0U;
constexpr uint32_t kReadOnly = 1U << 1U;
constexpr uint32_t kStorable = 1U << 2U;
constexpr uint32_t kPeakAvailable = 1U << 2U;
constexpr uint32_t kStreamFormatStorable = 1U << 12U;

} // namespace

bool DecodeDiceExtensionCaps(std::span<const uint8_t> wire,
                             DiceExtensionCaps& outCaps) noexcept {
    if (wire.size() < DiceExtensionCaps::kWireSize) return false;

    const uint32_t router = FW::ReadBE32(wire.data());
    const uint32_t mixer = FW::ReadBE32(wire.data() + 4);
    const uint32_t general = FW::ReadBE32(wire.data() + 8);

    outCaps = {};
    outCaps.router.exposed = (router & kExposed) != 0;
    outCaps.router.readOnly = (router & kReadOnly) != 0;
    outCaps.router.storable = (router & kStorable) != 0;
    outCaps.router.maximumEntryCount = static_cast<uint16_t>(router >> 16U);

    outCaps.mixer.exposed = (mixer & kExposed) != 0;
    outCaps.mixer.readOnly = (mixer & kReadOnly) != 0;
    outCaps.mixer.storable = (mixer & kStorable) != 0;
    outCaps.mixer.inputDeviceId = static_cast<uint8_t>((mixer >> 4U) & 0x0FU);
    outCaps.mixer.outputDeviceId = static_cast<uint8_t>((mixer >> 8U) & 0x0FU);
    outCaps.mixer.inputCount = static_cast<uint8_t>((mixer >> 16U) & 0xFFU);
    outCaps.mixer.outputCount = static_cast<uint8_t>(mixer >> 24U);

    outCaps.general.dynamicStreamFormat = (general & kExposed) != 0;
    outCaps.general.storageAvailable = (general & kReadOnly) != 0;
    outCaps.general.peakAvailable = (general & kPeakAvailable) != 0;
    outCaps.general.maximumTxStreams = static_cast<uint8_t>((general >> 4U) & 0x0FU);
    outCaps.general.maximumRxStreams = static_cast<uint8_t>((general >> 8U) & 0x0FU);
    outCaps.general.streamFormatStorable = (general & kStreamFormatStorable) != 0;
    outCaps.general.asicType = static_cast<uint16_t>(general >> 16U);
    return true;
}

bool DecodeDiceRouterEntry(std::span<const uint8_t> wire,
                           DiceRouterEntry& outEntry) noexcept {
    if (wire.size() < DiceRouterEntry::kWireSize) return false;

    const uint32_t value = FW::ReadBE32(wire.data());
    const uint8_t destination = static_cast<uint8_t>(value & 0xFFU);
    const uint8_t source = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    outEntry = {
        .destinationBlock = static_cast<uint8_t>(destination >> 4U),
        .destinationChannel = static_cast<uint8_t>(destination & 0x0FU),
        .sourceBlock = static_cast<uint8_t>(source >> 4U),
        .sourceChannel = static_cast<uint8_t>(source & 0x0FU),
        .peak = static_cast<uint16_t>(value >> 16U),
    };
    return true;
}

bool DecodeDiceMixerCoefficients(std::span<const uint8_t> wire,
                                 const DiceMixerCaps& caps,
                                 DiceMixerCoefficients& out) noexcept {
    if (wire.size() < kDiceMixerCoefficientWireBytes ||
        caps.inputCount > kDiceMaximumMixerInputs ||
        caps.outputCount > kDiceMaximumMixerOutputs) {
        return false;
    }

    out = {};
    out.inputCount = caps.inputCount;
    out.outputCount = caps.outputCount;
    for (uint8_t output = 0; output < caps.outputCount; ++output) {
        for (uint8_t input = 0; input < caps.inputCount; ++input) {
            const size_t wireOffset = size_t{4} *
                (size_t{output} * kDiceMaximumMixerInputs + input);
            out.values[size_t{output} * kDiceMaximumMixerInputs + input] =
                static_cast<uint16_t>(FW::ReadBE32(wire.data() + wireOffset));
        }
    }
    return true;
}

} // namespace ASFW::Audio::DICE
