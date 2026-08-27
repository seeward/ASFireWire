// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DICERouterImage.hpp -- building and editing a whole TCAT router image.
//
// The router section is written as one image: a leading entry count followed by
// the entries. On the Saffire Pro 24 DSP the staging router section reads back
// an entry count of zero, so an edit cannot be a read-modify-write of that
// section -- the image has to be built from the active CURRENT_CONFIG copy,
// modified, and written whole. Measured 2026-08-27.
//
// **Entry position is semantically meaningful.** The peak section is a parallel
// array of router entries indexed by position, so moving an entry moves what a
// meter reads. Consequently this layer offers in-place source replacement only:
// it never adds, removes, reorders or compacts entries. Adding a route is a
// larger design question -- which meter slot it displaces -- and is deliberately
// not expressible here rather than being available and silently wrong.

#pragma once

#include "DICEExtensionState.hpp"

#include <expected>
#include <span>

namespace ASFW::Audio::DICE {

/// Wire size of an image carrying `count` entries: the count quadlet plus one
/// quadlet per entry.
[[nodiscard]] constexpr size_t DiceRouterImageWireBytes(uint16_t count) noexcept {
    return 4U + (size_t{count} * DiceRouterEntry::kWireSize);
}

enum class DiceRouterEditError : uint8_t {
    /// No entry currently drives that destination. Creating one would change
    /// every later entry's position, and with it the peak section's meaning.
    DestinationNotPresent = 1,
    /// More than one entry drives it. Real destinations take exactly one
    /// source; the duplicates in a captured image are metering slots, which
    /// must not be retargeted.
    AmbiguousDestination,
    /// The entry lies in the profile's reserved leading range.
    ReservedEntry,
    /// The image does not fit the device's advertised bound, or the supplied
    /// buffer is too small.
    OutOfRange,
};

/// Serialises an image for the router section. Entries are written in their
/// existing order, including their peak halves, so an unmodified image
/// round-trips to exactly the bytes it was decoded from.
[[nodiscard]] std::expected<size_t, DiceRouterEditError>
EncodeDiceRouterImage(const DiceRouterEntries& image,
                      uint16_t maximumEntries,
                      std::span<uint8_t> wire) noexcept;

/// Index of the single entry driving `destinationBlock:destinationChannel`.
[[nodiscard]] std::expected<uint16_t, DiceRouterEditError>
FindDiceRouterDestination(const DiceRouterEntries& image,
                          uint8_t destinationBlock,
                          uint8_t destinationChannel) noexcept;

/// Replaces, in place, the source feeding one destination. `reservedEntries` is
/// the profile's count of leading entries that exist for metering rather than
/// for routing; an edit inside that range is refused.
[[nodiscard]] std::expected<void, DiceRouterEditError>
SetDiceRouterSource(DiceRouterEntries& image,
                    uint8_t destinationBlock,
                    uint8_t destinationChannel,
                    uint8_t sourceBlock,
                    uint8_t sourceChannel,
                    uint16_t reservedEntries) noexcept;

/// True when two images describe the same routing: same length, same entries,
/// in the same positions. The peak halves are ignored because they are live
/// meter readings, not configuration, and differ between two reads of an
/// otherwise identical image.
[[nodiscard]] bool DiceRouterImagesMatch(const DiceRouterEntries& left,
                                          const DiceRouterEntries& right) noexcept;

} // namespace ASFW::Audio::DICE
