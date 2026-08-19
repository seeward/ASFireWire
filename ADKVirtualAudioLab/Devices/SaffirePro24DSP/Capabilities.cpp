#include "Capabilities.hpp"

namespace ASFW::Devices::SaffirePro24DSP {

const Device::DeviceCapabilities& capabilities() {
    static const Device::DeviceCapabilities kCaps{
        .identity = {
            .manufacturer = "Focusrite",
            .model = "Saffire Pro 24 DSP",
        },
        .sampleRates = {44100, 48000, 88200, 96000},
        .optical = Device::OpticalCapabilities{
            .inputModes = {Device::OpticalMode::Adat, Device::OpticalMode::Spdif},
            .outputModes = {Device::OpticalMode::Adat, Device::OpticalMode::Spdif},
        },
    };
    return kCaps;
}

Device::DeviceConfiguration defaultConfiguration() {
    return Device::DeviceConfiguration{
        .sampleRate = 48000,
        .opticalInput = Device::OpticalMode::Adat,
        .opticalOutput = Device::OpticalMode::Adat,
    };
}

} // namespace ASFW::Devices::SaffirePro24DSP
