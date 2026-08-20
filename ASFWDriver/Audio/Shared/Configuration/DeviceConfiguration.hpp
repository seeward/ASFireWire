#pragma once

#include <cstdint>
#include <optional>

namespace ASFW::Configuration {

using SampleRate = uint32_t;

enum class OpticalMode : uint8_t {
    Adat,
    Spdif,
};

/// A hardware-facing audio configuration. It deliberately carries only
/// semantic controls; each endpoint resolves it into its own stream geometry.
struct DeviceConfiguration final {
    SampleRate sampleRate{48000};
    std::optional<OpticalMode> opticalInput;
    std::optional<OpticalMode> opticalOutput;
};

} // namespace ASFW::Configuration
