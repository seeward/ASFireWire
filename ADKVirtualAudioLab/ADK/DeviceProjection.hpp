#pragma once

#include "../Core/Device/Capabilities.hpp"
#include "../Core/Device/ResolvedAudioConfiguration.hpp"

#include <string>
#include <vector>

namespace ASFW::ADK {

struct ProjectedDeviceProperties {
    std::string deviceUID;
    std::string modelUID;
    std::string manufacturerUID;
    std::string name;
    std::vector<double> availableSampleRates;
    double currentSampleRate{48000.0};
    uint32_t transportType{0x31333934}; // '1394' FireWire
};

ProjectedDeviceProperties projectDevice(
    const Device::DeviceCapabilities& capabilities,
    const Device::ResolvedAudioConfiguration& resolved);

} // namespace ASFW::ADK
