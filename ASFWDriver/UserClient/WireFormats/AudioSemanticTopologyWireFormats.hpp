// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Topology/IAudioSemanticTopology.hpp"

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioSemanticTopologyWireVersion = 1;

struct AudioSemanticTopologySnapshotWire final {
    uint32_t version{kAudioSemanticTopologyWireVersion};
    uint32_t reserved{0};
    uint64_t endpointId{0};
    ASFW::Audio::AudioSemanticTopologySnapshot topology{};
};
static_assert(sizeof(AudioSemanticTopologySnapshotWire) < 4096,
              "semantic topology wire reply must fit the UserClient inline limit");

} // namespace ASFW::UserClient::Wire
