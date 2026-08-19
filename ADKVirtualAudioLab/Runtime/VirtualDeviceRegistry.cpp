#include "VirtualDeviceRegistry.hpp"

#include "../Devices/Duet/Capabilities.hpp"
#include "../Devices/Duet/Resolve.hpp"
#include "../Devices/Phase88/Capabilities.hpp"
#include "../Devices/Phase88/Resolve.hpp"
#include "../Devices/FW1814/Capabilities.hpp"
#include "../Devices/FW1814/Resolve.hpp"
#include "../Devices/SaffirePro24DSP/Capabilities.hpp"
#include "../Devices/SaffirePro24DSP/Resolve.hpp"

#include <array>

namespace ASFW::Runtime {

namespace {

const std::array<VirtualDeviceDefinition, 4> kDefinitions = {{
    VirtualDeviceDefinition{
        VirtualDeviceKind::Duet,
        &Devices::Duet::capabilities,
        &Devices::Duet::defaultConfiguration,
        &Devices::Duet::resolve,
        &Devices::Duet::makeInitialState,
    },
    VirtualDeviceDefinition{
        VirtualDeviceKind::Phase88,
        &Devices::Phase88::capabilities,
        &Devices::Phase88::defaultConfiguration,
        &Devices::Phase88::resolve,
        &Devices::Phase88::makeInitialState,
    },
    VirtualDeviceDefinition{
        VirtualDeviceKind::FW1814,
        &Devices::FW1814::capabilities,
        &Devices::FW1814::defaultConfiguration,
        &Devices::FW1814::resolve,
        &Devices::FW1814::makeInitialState,
    },
    VirtualDeviceDefinition{
        VirtualDeviceKind::SaffirePro24DSP,
        &Devices::SaffirePro24DSP::capabilities,
        &Devices::SaffirePro24DSP::defaultConfiguration,
        &Devices::SaffirePro24DSP::resolve,
        &Devices::SaffirePro24DSP::makeInitialState,
    },
}};

} // namespace

std::span<const VirtualDeviceDefinition> virtualDevices() {
    return kDefinitions;
}

const VirtualDeviceDefinition* findVirtualDevice(VirtualDeviceKind kind) {
    for (const auto& def : kDefinitions) {
        if (def.kind == kind) {
            return &def;
        }
    }
    return nullptr;
}

} // namespace ASFW::Runtime
