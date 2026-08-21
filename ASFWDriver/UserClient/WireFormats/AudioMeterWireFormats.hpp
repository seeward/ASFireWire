// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Metering/IAudioMetering.hpp"

#include <array>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioMeterWireVersion = 1;

struct AudioMeterSnapshotWire final {
    uint32_t version{kAudioMeterWireVersion};
    uint32_t revision{0};
    uint64_t endpointId{0};
    uint32_t valueCount{0};
    uint32_t detectedSampleRateHz{0};
    uint8_t enabled{0};
    uint8_t clockLocked{0};
    uint8_t _reserved[2]{};
    std::array<int16_t, ASFW::Audio::kMaxAudioMeterValues> values{};
};
static_assert(sizeof(AudioMeterSnapshotWire) == 112);

} // namespace ASFW::UserClient::Wire
