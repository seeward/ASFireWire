#pragma once

#include "DeviceIdentity.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ASFW::Device {

using SampleRate = uint32_t;

enum class OpticalMode {
    Adat,
    Spdif,
};

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
