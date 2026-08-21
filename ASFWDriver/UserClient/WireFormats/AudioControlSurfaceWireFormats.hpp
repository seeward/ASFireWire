// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Controls/IAudioControlSurface.hpp"

#include <array>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioControlSurfaceWireVersion = 1;

struct AudioControlValueWire final {
    uint32_t id{0};
    int32_t value{0};
};
static_assert(sizeof(AudioControlValueWire) == 8);

struct AudioControlSurfaceSnapshotWire final {
    uint32_t version{kAudioControlSurfaceWireVersion};
    uint32_t kind{0};
    uint64_t endpointId{0};
    uint32_t revision{0};
    uint32_t valueCount{0};
    std::array<AudioControlValueWire, ASFW::Audio::kMaxAudioControlSurfaceValues> values{};
};
static_assert(sizeof(AudioControlSurfaceSnapshotWire) == 152);

} // namespace ASFW::UserClient::Wire
