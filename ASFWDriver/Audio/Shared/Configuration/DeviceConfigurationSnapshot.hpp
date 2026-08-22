// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "DeviceConfiguration.hpp"

#include <array>
#include <cstdint>

namespace ASFW::Configuration {

inline constexpr uint32_t kDeviceConfigurationSnapshotVersion = 2;
inline constexpr size_t kMaxConfigurationSnapshotCapabilities = 8;

// Value-only query contract for the app/user-client boundary.  It carries no
// protocol object, buffer, or pointer across the DriverKit boundary.
struct DeviceConfigurationCapabilitySnapshot final {
    DeviceConfiguration configuration{};
    uint32_t inputChannels{0};
    uint32_t outputChannels{0};
};

struct DeviceConfigurationSnapshot final {
    uint32_t version{kDeviceConfigurationSnapshotVersion};
    uint64_t endpointId{0};
    uint64_t topologyRevision{0};
    DeviceConfiguration committed{};
    uint32_t inputChannels{0};
    uint32_t outputChannels{0};
    std::array<DeviceConfigurationCapabilitySnapshot,
               kMaxConfigurationSnapshotCapabilities> capabilities{};
    uint8_t capabilityCount{0};
};

} // namespace ASFW::Configuration
