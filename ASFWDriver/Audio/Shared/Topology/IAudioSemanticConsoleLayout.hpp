// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// IAudioSemanticConsoleLayout.hpp -- bounded, topology-revisioned console
// projection.  This complements the signal graph when a device has more
// controls/meters than the immutable graph reply can carry in one UserClient
// call.  IDs are opaque semantic IDs: vendor register offsets and encodings
// remain below the protocol boundary.

#pragma once

#include "IAudioSemanticTopology.hpp"

#include <array>
#include <cstdint>
#include <expected>

namespace ASFW::Audio {

enum class AudioSemanticConsoleStripKind : uint8_t {
    Input = 1,
    Playback = 2,
    Output = 3,
    Auxiliary = 4,
    Headphone = 5,
};

enum class AudioSemanticConsoleSourceKind : uint8_t {
    None = 0,
    MixerOrAux = 1,
    Mixer12Mixer34OrAux = 2,
};

enum class AudioSemanticConsoleBus : uint8_t {
    Main12 = 1,
    Main34 = 2,
};

enum AudioSemanticConsoleStripFlag : uint8_t {
    kAudioSemanticConsoleStripLinkable = 1U << 0U,
    kAudioSemanticConsoleStripMuteable = 1U << 1U,
    kAudioSemanticConsoleStripSoloable = 1U << 2U,
    kAudioSemanticConsoleStripAssignable = 1U << 3U,
};

/// A stereo (or otherwise channel-grouped) console strip. A non-zero control
/// ID names the first per-channel value; following channels use consecutive
/// semantic IDs. This models a channel group without exposing its protocol
/// packing to the UI.
struct AudioSemanticConsoleStrip final {
    uint32_t id{0};
    AudioSemanticConsoleStripKind kind{AudioSemanticConsoleStripKind::Input};
    AudioSemanticSignalKind signalKind{AudioSemanticSignalKind::None};
    uint8_t channelCount{0};
    uint8_t flags{0};
    uint32_t firstSignalIndex{0};
    uint32_t levelControlId{0};
    uint32_t panControlId{0};
    uint32_t auxControlId{0};
    uint32_t sourceControlId{0};
    AudioSemanticConsoleSourceKind sourceKind{AudioSemanticConsoleSourceKind::None};
    uint8_t meterCount{0};
    uint16_t meterFirstIndex{0};
};
static_assert(sizeof(AudioSemanticConsoleStrip) == 40);

/// A switchable contribution from a source strip into one monitor-mixer bus.
/// The mask is private control-state representation owned by the driver; the
/// UI only feeds it back to the named semantic control.
struct AudioSemanticConsoleCrosspoint final {
    uint32_t id{0};
    uint32_t sourceStripId{0};
    AudioSemanticConsoleBus destinationBus{AudioSemanticConsoleBus::Main12};
    uint8_t _reserved[3]{};
    uint32_t controlId{0};
    uint32_t enabledMask{0};
};
static_assert(sizeof(AudioSemanticConsoleCrosspoint) == 20);

inline constexpr uint32_t kAudioSemanticConsoleLayoutVersion = 1;
inline constexpr size_t kMaxAudioSemanticConsoleStrips = 20;
inline constexpr size_t kMaxAudioSemanticConsoleCrosspoints = 32;

struct AudioSemanticConsoleLayoutSnapshot final {
    uint32_t version{kAudioSemanticConsoleLayoutVersion};
    uint32_t deviceKind{0};
    uint64_t topologyRevision{0};
    uint32_t stripCount{0};
    uint32_t crosspointCount{0};
    std::array<AudioSemanticConsoleStrip, kMaxAudioSemanticConsoleStrips> strips{};
    std::array<AudioSemanticConsoleCrosspoint, kMaxAudioSemanticConsoleCrosspoints> crosspoints{};
};
static_assert(sizeof(AudioSemanticConsoleLayoutSnapshot) == 1464);

class IAudioSemanticConsoleLayout {
public:
    virtual ~IAudioSemanticConsoleLayout() = default;
    [[nodiscard]] virtual bool CopyAudioSemanticConsoleLayout(
        AudioSemanticConsoleLayoutSnapshot& outSnapshot) const noexcept = 0;
};

enum class AudioSemanticConsoleLayoutValidationError : uint32_t {
    InvalidVersion,
    MissingDeviceKind,
    MissingTopologyRevision,
    CountOutOfRange,
    InvalidStrip,
    DuplicateStripId,
    InvalidCrosspoint,
    DuplicateCrosspointId,
    DuplicateCrosspoint,
};

namespace Detail {

[[nodiscard]] constexpr bool IsValid(AudioSemanticConsoleStripKind kind) noexcept {
    return kind == AudioSemanticConsoleStripKind::Input ||
           kind == AudioSemanticConsoleStripKind::Playback ||
           kind == AudioSemanticConsoleStripKind::Output ||
           kind == AudioSemanticConsoleStripKind::Auxiliary ||
           kind == AudioSemanticConsoleStripKind::Headphone;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticConsoleSourceKind kind) noexcept {
    return kind == AudioSemanticConsoleSourceKind::None ||
           kind == AudioSemanticConsoleSourceKind::MixerOrAux ||
           kind == AudioSemanticConsoleSourceKind::Mixer12Mixer34OrAux;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticConsoleBus bus) noexcept {
    return bus == AudioSemanticConsoleBus::Main12 || bus == AudioSemanticConsoleBus::Main34;
}

[[nodiscard]] constexpr const AudioSemanticConsoleStrip* FindConsoleStrip(
    const AudioSemanticConsoleLayoutSnapshot& snapshot, uint32_t id) noexcept {
    for (uint32_t index = 0; index < snapshot.stripCount; ++index) {
        if (snapshot.strips[index].id == id) return &snapshot.strips[index];
    }
    return nullptr;
}

} // namespace Detail

[[nodiscard]] constexpr std::expected<void, AudioSemanticConsoleLayoutValidationError>
ValidateAudioSemanticConsoleLayout(const AudioSemanticConsoleLayoutSnapshot& snapshot) noexcept {
    using Error = AudioSemanticConsoleLayoutValidationError;
    if (snapshot.version != kAudioSemanticConsoleLayoutVersion) {
        return std::unexpected(Error::InvalidVersion);
    }
    if (snapshot.deviceKind == 0) return std::unexpected(Error::MissingDeviceKind);
    if (snapshot.topologyRevision == 0) return std::unexpected(Error::MissingTopologyRevision);
    if (snapshot.stripCount > kMaxAudioSemanticConsoleStrips ||
        snapshot.crosspointCount > kMaxAudioSemanticConsoleCrosspoints) {
        return std::unexpected(Error::CountOutOfRange);
    }
    for (uint32_t index = 0; index < snapshot.stripCount; ++index) {
        const auto& strip = snapshot.strips[index];
        const bool hasSource = strip.sourceControlId != 0;
        if (strip.id == 0 || !Detail::IsValid(strip.kind) || !Detail::IsValid(strip.signalKind) ||
            strip.signalKind == AudioSemanticSignalKind::None || strip.channelCount == 0 ||
            strip.firstSignalIndex == 0 || strip.levelControlId == 0 ||
            !Detail::IsValid(strip.sourceKind) || hasSource !=
                (strip.sourceKind != AudioSemanticConsoleSourceKind::None) ||
            strip.meterCount > strip.channelCount ||
            (strip.flags & ~uint8_t{kAudioSemanticConsoleStripLinkable |
                                    kAudioSemanticConsoleStripMuteable |
                                    kAudioSemanticConsoleStripSoloable |
                                    kAudioSemanticConsoleStripAssignable}) != 0) {
            return std::unexpected(Error::InvalidStrip);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.strips[earlier].id == strip.id) {
                return std::unexpected(Error::DuplicateStripId);
            }
        }
    }
    for (uint32_t index = 0; index < snapshot.crosspointCount; ++index) {
        const auto& crosspoint = snapshot.crosspoints[index];
        if (crosspoint.id == 0 || !Detail::FindConsoleStrip(snapshot, crosspoint.sourceStripId) ||
            !Detail::IsValid(crosspoint.destinationBus) || crosspoint.controlId == 0 ||
            crosspoint.enabledMask == 0) {
            return std::unexpected(Error::InvalidCrosspoint);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            const auto& previous = snapshot.crosspoints[earlier];
            if (previous.id == crosspoint.id) return std::unexpected(Error::DuplicateCrosspointId);
            if (previous.sourceStripId == crosspoint.sourceStripId &&
                previous.destinationBus == crosspoint.destinationBus) {
                return std::unexpected(Error::DuplicateCrosspoint);
            }
        }
    }
    return {};
}

} // namespace ASFW::Audio
