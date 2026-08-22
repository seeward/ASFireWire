// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Topology/IAudioSemanticTopology.hpp"

#include <array>
#include <cstddef>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioSemanticTopologyWireVersion = 1;
inline constexpr uint32_t kAudioSemanticTopologyEndpointListWireVersion = 1;

struct AudioSemanticTopologyEndpointListWire final {
    uint32_t version{kAudioSemanticTopologyEndpointListWireVersion};
    uint32_t endpointCount{0};
    std::array<uint64_t, ASFW::Audio::kMaxAudioSemanticTopologyEndpoints> endpointIds{};
};
static_assert(sizeof(AudioSemanticTopologyEndpointListWire) == 72);

struct AudioSemanticTopologySnapshotWire final {
    uint32_t version{kAudioSemanticTopologyWireVersion};
    uint32_t reserved{0};
    uint64_t endpointId{0};
    ASFW::Audio::AudioSemanticTopologySnapshot topology{};
};
static_assert(offsetof(AudioSemanticTopologySnapshotWire, topology) == 16);
static_assert(sizeof(AudioSemanticTopologySnapshotWire) == 3496,
              "semantic topology wire ABI changed");

} // namespace ASFW::UserClient::Wire
