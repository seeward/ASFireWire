#pragma once

#include "../../Core/Device/Capabilities.hpp"
#include "../../Core/Device/Configuration.hpp"

namespace ASFW::Devices::SaffirePro24DSP {

const Device::DeviceCapabilities& capabilities();
Device::DeviceConfiguration defaultConfiguration();

} // namespace ASFW::Devices::SaffirePro24DSP
