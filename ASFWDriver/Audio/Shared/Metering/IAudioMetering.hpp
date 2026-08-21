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

struct AudioMeterSnapshot final {
    uint32_t revision{0};
    uint32_t valueCount{0};
    uint32_t detectedSampleRateHz{0};
    bool enabled{false};
    bool clockLocked{false};
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
