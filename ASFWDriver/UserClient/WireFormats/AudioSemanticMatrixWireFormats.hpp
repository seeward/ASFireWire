// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Audio/Shared/Topology/IAudioSemanticMatrix.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioSemanticMatrixWireVersion = 1;
inline constexpr uint32_t kAudioSemanticMatrixEndpointListWireVersion = 1;

struct AudioSemanticMatrixEndpointListWire final {
    uint32_t version{kAudioSemanticMatrixEndpointListWireVersion};
    uint32_t endpointCount{0};
    std::array<uint64_t, ASFW::Audio::kMaxAudioSemanticMatrixEndpoints> endpointIds{};
};
static_assert(sizeof(AudioSemanticMatrixEndpointListWire) == 72);

struct AudioSemanticMatrixSnapshotWire final {
    uint32_t version{kAudioSemanticMatrixWireVersion};
    uint32_t reserved{0};
    uint64_t endpointId{0};
    ASFW::Audio::AudioSemanticMatrixSnapshot matrix{};
};
static_assert(offsetof(AudioSemanticMatrixSnapshotWire, matrix) == 16);
static_assert(sizeof(AudioSemanticMatrixSnapshotWire) == 1784);

} // namespace ASFW::UserClient::Wire
