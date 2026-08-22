#include "Capabilities.hpp"

namespace ASFW::Devices::Duet {

const Device::DeviceCapabilities& capabilities() {
    static const Device::DeviceCapabilities kCaps{
        .identity = {
            .manufacturer = "Apogee",
            .model = "Duet FireWire",
        },
        // These are the rates the current OXFW backend can actually apply and
        // confirm.  Do not advertise the wider OXFW-chip rate table here: a
        // topology fixture is also the contract offered to the UI.
        .sampleRates = {32000, 44100, 48000},
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
