#pragma once

#include "Capabilities.hpp"

#include <optional>
#include <string>

namespace ASFW::Device {

struct DeviceConfiguration {
    SampleRate sampleRate{48000};
    std::optional<OpticalMode> opticalInput;
    std::optional<OpticalMode> opticalOutput;
};

enum class ResolveErrorKind {
    UnsupportedSampleRate,
    UnsupportedOpticalMode,
    InvalidConfiguration,
};

struct ResolveError {
    ResolveErrorKind kind;
    std::string message;
};

} // namespace ASFW::Device
