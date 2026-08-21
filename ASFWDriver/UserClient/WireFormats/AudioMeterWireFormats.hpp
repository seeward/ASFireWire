// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Metering/IAudioMetering.hpp"

#include <array>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioMeterWireVersion = 2;

struct AudioMeterSnapshotWire final {
    uint32_t version{kAudioMeterWireVersion};
    uint32_t revision{0};
    uint64_t endpointId{0};
    uint32_t valueCount{0};
    uint32_t detectedSampleRateHz{0};
    uint8_t enabled{0};
    uint8_t clockLocked{0};
    uint8_t externalSync{0};
    uint8_t hardwareSwitch{0};
    uint32_t rotaryCount{0};
    std::array<int16_t, ASFW::Audio::kMaxAudioMeterRotaries> rotaries{};
    uint16_t _reserved{0};
    std::array<int16_t, ASFW::Audio::kMaxAudioMeterValues> values{};
};
static_assert(sizeof(AudioMeterSnapshotWire) == 120);

} // namespace ASFW::UserClient::Wire
