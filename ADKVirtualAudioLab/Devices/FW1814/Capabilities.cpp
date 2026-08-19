#include "Capabilities.hpp"

namespace ASFW::Devices::FW1814 {

const Device::DeviceCapabilities& capabilities() {
    static const Device::DeviceCapabilities kCaps{
        .identity = {
            .manufacturer = "M-Audio",
            .model = "FireWire 1814",
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

} // namespace ASFW::Devices::FW1814
