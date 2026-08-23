// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Topology/IAudioSemanticConsoleLayout.hpp"

#include <cstddef>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioSemanticConsoleLayoutWireVersion = 1;

struct AudioSemanticConsoleLayoutSnapshotWire final {
    uint32_t version{kAudioSemanticConsoleLayoutWireVersion};
    uint32_t reserved{0};
    uint64_t endpointId{0};
    ASFW::Audio::AudioSemanticConsoleLayoutSnapshot layout{};
};
static_assert(offsetof(AudioSemanticConsoleLayoutSnapshotWire, layout) == 16);
static_assert(sizeof(AudioSemanticConsoleLayoutSnapshotWire) == 1480);

} // namespace ASFW::UserClient::Wire
