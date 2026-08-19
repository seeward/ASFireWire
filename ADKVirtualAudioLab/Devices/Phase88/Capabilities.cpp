#include "Capabilities.hpp"

namespace ASFW::Devices::Phase88 {

const Device::DeviceCapabilities& capabilities() {
    static const Device::DeviceCapabilities kCaps{
        .identity = {
            .manufacturer = "TerraTec",
            .model = "PHASE 88 Rack FW",
        },
        .sampleRates = {32000, 44100, 48000, 88200, 96000},
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

} // namespace ASFW::Devices::Phase88
