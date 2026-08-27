// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "DICERouterImage.hpp"

#include "../../../../Common/WireFormat.hpp"

namespace ASFW::Audio::DICE {

namespace {

/// One entry packs into a quadlet as destination byte, source byte, then the
/// peak half-word, each byte splitting into a block nibble and a channel
/// nibble. Mirrors DecodeDiceRouterEntry exactly; cross-validated with the
/// local ALSA control reference,
/// protocols/dice/src/tcat/extension/router_entry.rs:6-20,81-95.
[[nodiscard]] uint32_t EncodeEntry(const DiceRouterEntry& entry) noexcept {
    const auto destination = static_cast<uint32_t>(
        ((entry.destinationBlock & 0x0FU) << 4U) | (entry.destinationChannel & 0x0FU));
    const auto source = static_cast<uint32_t>(
        ((entry.sourceBlock & 0x0FU) << 4U) | (entry.sourceChannel & 0x0FU));
    return (static_cast<uint32_t>(entry.peak) << 16U) | (source << 8U) | destination;
}

} // namespace

std::expected<size_t, DiceRouterEditError> EncodeDiceRouterImage(
    const DiceRouterEntries& image,
    uint16_t maximumEntries,
    std::span<uint8_t> wire) noexcept {
    if (image.count > kDiceMaximumRouterEntries || image.count > maximumEntries) {
        return std::unexpected(DiceRouterEditError::OutOfRange);
    }
    const size_t bytes = DiceRouterImageWireBytes(image.count);
    if (wire.size() < bytes) return std::unexpected(DiceRouterEditError::OutOfRange);

    FW::WriteBE32(wire.data(), image.count);
    for (uint16_t index = 0; index < image.count; ++index) {
        FW::WriteBE32(wire.data() + 4U + (size_t{index} * DiceRouterEntry::kWireSize),
                      EncodeEntry(image.entries[index]));
    }
    return bytes;
}

std::expected<uint16_t, DiceRouterEditError> FindDiceRouterDestination(
    const DiceRouterEntries& image,
    uint8_t destinationBlock,
    uint8_t destinationChannel) noexcept {
    if (image.count > kDiceMaximumRouterEntries) {
        return std::unexpected(DiceRouterEditError::OutOfRange);
    }
    bool found = false;
    uint16_t match = 0;
    for (uint16_t index = 0; index < image.count; ++index) {
        const auto& entry = image.entries[index];
        if (entry.destinationBlock != destinationBlock ||
            entry.destinationChannel != destinationChannel) {
            continue;
        }
        // A captured image contains destinations that appear twice: they are
        // metering slots, not routes, and retargeting either one would change
        // what a meter reads rather than where audio goes.
        if (found) return std::unexpected(DiceRouterEditError::AmbiguousDestination);
        found = true;
        match = index;
    }
    if (!found) return std::unexpected(DiceRouterEditError::DestinationNotPresent);
    return match;
}

std::expected<void, DiceRouterEditError> SetDiceRouterSource(
    DiceRouterEntries& image,
    uint8_t destinationBlock,
    uint8_t destinationChannel,
    uint8_t sourceBlock,
    uint8_t sourceChannel,
    uint16_t reservedEntries) noexcept {
    const auto index = FindDiceRouterDestination(image, destinationBlock, destinationChannel);
    if (!index) return std::unexpected(index.error());
    if (*index < reservedEntries) {
        return std::unexpected(DiceRouterEditError::ReservedEntry);
    }

    auto& entry = image.entries[*index];
    entry.sourceBlock = static_cast<uint8_t>(sourceBlock & 0x0FU);
    entry.sourceChannel = static_cast<uint8_t>(sourceChannel & 0x0FU);
    // The peak half is a live meter reading rather than configuration. Leaving
    // the decoded value in place keeps an unmodified image byte-identical on
    // re-encode, which is what makes a verify-by-readback meaningful.
    return {};
}

bool DiceRouterImagesMatch(const DiceRouterEntries& left,
                            const DiceRouterEntries& right) noexcept {
    if (left.count != right.count || left.count > kDiceMaximumRouterEntries) return false;
    for (uint16_t index = 0; index < left.count; ++index) {
        const auto& lhs = left.entries[index];
        const auto& rhs = right.entries[index];
        if (lhs.destinationBlock != rhs.destinationBlock ||
            lhs.destinationChannel != rhs.destinationChannel ||
            lhs.sourceBlock != rhs.sourceBlock ||
            lhs.sourceChannel != rhs.sourceChannel) {
            return false;
        }
    }
    return true;
}

} // namespace ASFW::Audio::DICE
