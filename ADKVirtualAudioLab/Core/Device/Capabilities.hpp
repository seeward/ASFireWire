#pragma once

#include "DeviceIdentity.hpp"
#include "../../../ASFWDriver/Audio/Shared/Configuration/DeviceConfiguration.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ASFW::Device {

using SampleRate = ::ASFW::Configuration::SampleRate;
using OpticalMode = ::ASFW::Configuration::OpticalMode;

struct OpticalCapabilities {
    std::vector<OpticalMode> inputModes;
    std::vector<OpticalMode> outputModes;
};

struct DeviceCapabilities {
    DeviceIdentity identity;
    std::vector<SampleRate> sampleRates;
    std::optional<OpticalCapabilities> optical;
};

} // namespace ASFW::Device
