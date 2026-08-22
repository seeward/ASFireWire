// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// IAudioControlSurface.hpp — bounded semantic control surface for a resolved
// audio endpoint. The user client sees named values, never a vendor register
// address or an arbitrary async transaction.

#pragma once

#include <DriverKit/IOReturn.h>

#include <array>
#include <cstdint>
#include <functional>

namespace ASFW::Audio {

enum class AudioControlSurfaceKind : uint32_t {
    None = 0,
    MAudio1814Mixer = 0x4D41'3134, // "MA14"
    /// Values are semantic parameter IDs; their definitions come from the
    /// accompanying IAudioSemanticTopology snapshot, never from this enum.
    ApogeeDuet = 0x4455'4554, // "DUET"
};

struct AudioControlValue final {
    uint32_t id{0};
    int32_t value{0};
};

// The FireWire 1814's parameter window alone names 78 controls (18 ranges over
// 160 bytes). Sized to hold a whole device surface in one snapshot so the UI
// never has to page — 80 values is 664 wire bytes, well inside the 4096-byte
// inline callStruct limit.
inline constexpr size_t kMaxAudioControlSurfaceValues = 80;

// A copied snapshot is always driver-side belief. Some legacy devices, notably
// the M-Audio special firmware, intentionally have no safe readback path.
struct AudioControlSurfaceSnapshot final {
    AudioControlSurfaceKind kind{AudioControlSurfaceKind::None};
    /// The resolved structure this state was copied against.
    uint64_t topologyRevision{0};
    uint32_t stateRevision{0};
    uint32_t valueCount{0};
    std::array<AudioControlValue, kMaxAudioControlSurfaceValues> values{};
};

class IAudioControlSurface {
public:
    using ApplyCallback = std::function<void(IOReturn)>;

    virtual ~IAudioControlSurface() = default;
    [[nodiscard]] virtual bool CopyAudioControlSurfaceSnapshot(
        AudioControlSurfaceSnapshot& outSnapshot) const noexcept = 0;
    virtual void ApplyAudioControlValue(uint32_t controlId, int32_t value,
                                        ApplyCallback callback) = 0;
};

} // namespace ASFW::Audio
