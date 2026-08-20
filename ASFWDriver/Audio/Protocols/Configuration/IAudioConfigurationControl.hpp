// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "../../Protocols/AudioTypes.hpp"
#include "../../Shared/Configuration/DeviceConfiguration.hpp"

#include <DriverKit/IOReturn.h>

#include <functional>

namespace ASFW::Audio {

// Result from a device protocol after it has accepted a semantic configuration.
// The coordinator resolves the candidate against the immutable profile first;
// this is the runtime/wire confirmation produced by the only layer permitted to
// talk to the hardware.
struct AudioConfigurationApplyResult final {
    Configuration::DeviceConfiguration configuration{};
    AudioStreamRuntimeCaps runtimeCaps{};
};

class IAudioConfigurationControl {
public:
    using ApplyCallback = std::function<void(IOReturn, AudioConfigurationApplyResult)>;

    virtual ~IAudioConfigurationControl() = default;

    [[nodiscard]] virtual bool SupportsConfiguration(
        const Configuration::DeviceConfiguration& configuration) const noexcept = 0;

    // Exactly one callback for every accepted request. A successful callback
    // means the protocol's state is now shaped for `configuration`; it does not
    // grant permission to mutate AudioDriverKit objects outside its Perform
    // window.
    virtual void ApplyConfiguration(
        const Configuration::DeviceConfiguration& configuration,
        ApplyCallback callback) = 0;

    // These protocols often have write-only control registers. This reports the
    // coherent driver-side belief only; a caller must not present it as an
    // independent hardware readback.
    [[nodiscard]] virtual AudioConfigurationApplyResult
    CurrentConfiguration() const noexcept = 0;
};

} // namespace ASFW::Audio
