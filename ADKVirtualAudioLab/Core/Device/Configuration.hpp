#pragma once

#include "Capabilities.hpp"
#include "../../../ASFWDriver/Audio/Shared/Configuration/DeviceConfiguration.hpp"

#include <string>

namespace ASFW::Device {

using DeviceConfiguration = ::ASFW::Configuration::DeviceConfiguration;

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
