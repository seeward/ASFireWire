// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include <array>
#include <cstdint>

namespace ASFW::UserClient::Wire {

inline constexpr uint32_t kAudioConfigurationWireVersion = 1;
inline constexpr uint32_t kAudioConfigurationMaxCapabilities = 8;

// opticalInput / opticalOutput: 0 = none, 1 = ADAT, 2 = S/PDIF.
struct AudioConfigurationCapabilityWire final {
    uint32_t sampleRateHz{0};
    uint32_t inputChannels{0};
    uint32_t outputChannels{0};
    uint8_t opticalInput{0};
    uint8_t opticalOutput{0};
    uint8_t _reserved[2]{};
};
static_assert(sizeof(AudioConfigurationCapabilityWire) == 16);

struct AudioConfigurationSnapshotWire final {
    uint32_t version{kAudioConfigurationWireVersion};
    uint32_t capabilityCount{0};
    uint64_t endpointId{0};
    AudioConfigurationCapabilityWire committed{};
    std::array<AudioConfigurationCapabilityWire,
               kAudioConfigurationMaxCapabilities> capabilities{};
};
static_assert(sizeof(AudioConfigurationSnapshotWire) == 160);

} // namespace ASFW::UserClient::Wire
