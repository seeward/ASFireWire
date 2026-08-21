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
    uint32_t revision{0};
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
    /// Integrated positions of relative encoders. Meaningful only as a running
    /// total since metering was enabled — the device sends detents, not a
    /// position, so this is not a readback.
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
