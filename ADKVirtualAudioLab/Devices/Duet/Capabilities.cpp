#include "Capabilities.hpp"

namespace ASFW::Devices::Duet {

const Device::DeviceCapabilities& capabilities() {
    static const Device::DeviceCapabilities kCaps{
        .identity = {
            .manufacturer = "Apogee",
            .model = "Duet FireWire",
        },
        .sampleRates = {44100, 48000, 88200, 96000},
        .optical = std::nullopt,
    };
    return kCaps;
}

Device::DeviceConfiguration defaultConfiguration() {
    return Device::DeviceConfiguration{
        .sampleRate = 48000,
        .opticalInput = std::nullopt,
        .opticalOutput = std::nullopt,
    };
}

} // namespace ASFW::Devices::Duet
