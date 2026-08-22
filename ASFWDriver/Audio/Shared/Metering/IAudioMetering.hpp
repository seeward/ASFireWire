// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// IAudioMetering.hpp — endpoint-owned, read-only meter state.

#pragma once

#include <DriverKit/IOReturn.h>

#include <array>
#include <cstdint>

namespace ASFW::Audio {

inline constexpr size_t kMaxAudioMeterValues = 40;

/// Number of relative hardware encoders a device may report. The FireWire 1814
/// has three; nothing here has more.
inline constexpr size_t kMaxAudioMeterRotaries = 3;

struct AudioMeterSnapshot final {
    /// The resolved meter-definition table this frame indexes.
    uint64_t topologyRevision{0};
    /// High-rate telemetry sequence; distinct from topology and control state.
    uint32_t telemetrySequence{0};
    uint32_t valueCount{0};
    uint32_t detectedSampleRateHz{0};
    bool enabled{false};
    bool clockLocked{false};
    /// The device reports it is slaved to an external clock reference. Distinct
    /// from `clockLocked`, which only says a rate was decodable at all.
    bool externalSync{false};
    /// Latched state of a front-panel toggle, where the device reports presses
    /// rather than position.
    bool hardwareSwitch{false};
    uint32_t rotaryCount{0};
    /// Bit-preserving 16-bit wrapping counters for relative encoder detents.
    /// Meaningful only as a modular delta since the preceding snapshot; the
    /// device sends events, not a physical position.
    std::array<int16_t, kMaxAudioMeterRotaries> rotaries{};
    std::array<int16_t, kMaxAudioMeterValues> values{};
};

class IAudioMetering {
public:
    virtual ~IAudioMetering() = default;
    [[nodiscard]] virtual bool CopyAudioMeterSnapshot(
        AudioMeterSnapshot& outSnapshot) const noexcept = 0;
    [[nodiscard]] virtual IOReturn SetAudioMeteringEnabled(bool enabled) noexcept = 0;
};

} // namespace ASFW::Audio
