#include "DeviceProjection.hpp"

namespace ASFW::ADK {

ProjectedDeviceProperties projectDevice(
    const Device::DeviceCapabilities& capabilities,
    const Device::ResolvedAudioConfiguration& resolved) {

    ProjectedDeviceProperties props;
    props.name = capabilities.identity.manufacturer + " " + capabilities.identity.model;
    props.manufacturerUID = capabilities.identity.manufacturer;
    props.modelUID = capabilities.identity.model;
    props.deviceUID = "net.mrmidi.asfw.virtual." + capabilities.identity.manufacturer + "." + capabilities.identity.model;

    props.availableSampleRates.reserve(capabilities.sampleRates.size());
    for (auto rate : capabilities.sampleRates) {
        props.availableSampleRates.push_back(static_cast<double>(rate));
    }

    props.currentSampleRate = static_cast<double>(resolved.streams.sampleRate);
    return props;
}

} // namespace ASFW::ADK
