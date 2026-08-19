#pragma once

#include "../Core/AudioModel/State.hpp"
#include "../Core/Device/Capabilities.hpp"
#include "../Core/Device/Configuration.hpp"
#include "../Core/Device/ResolvedAudioConfiguration.hpp"

#include <expected>
#include <span>

namespace ASFW::Runtime {

enum class VirtualDeviceKind {
    Duet,
    Phase88,
    FW1814,
    SaffirePro24DSP,
};

struct VirtualDeviceDefinition {
    VirtualDeviceKind kind;
    const Device::DeviceCapabilities& (*capabilities)();
    Device::DeviceConfiguration (*defaultConfiguration)();
    std::expected<Device::ResolvedAudioConfiguration, Device::ResolveError> (*resolve)(const Device::DeviceConfiguration&);
    AudioModel::DeviceState (*makeInitialState)(const Device::ResolvedAudioConfiguration&);
};

std::span<const VirtualDeviceDefinition> virtualDevices();
const VirtualDeviceDefinition* findVirtualDevice(VirtualDeviceKind kind);

} // namespace ASFW::Runtime
