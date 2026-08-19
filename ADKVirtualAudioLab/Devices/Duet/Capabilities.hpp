#pragma once

#include "../../Core/Device/Capabilities.hpp"
#include "../../Core/Device/Configuration.hpp"

namespace ASFW::Devices::Duet {

const Device::DeviceCapabilities& capabilities();
Device::DeviceConfiguration defaultConfiguration();

} // namespace ASFW::Devices::Duet
