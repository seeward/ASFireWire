#pragma once

#include "../../Core/AudioModel/State.hpp"
#include "../../Core/Device/Configuration.hpp"
#include "../../Core/Device/ResolvedAudioConfiguration.hpp"

#include <expected>

namespace ASFW::Devices::FW1814 {

std::expected<Device::ResolvedAudioConfiguration, Device::ResolveError> resolve(
    const Device::DeviceConfiguration& config);

AudioModel::DeviceState makeInitialState(
    const Device::ResolvedAudioConfiguration& resolved);

} // namespace ASFW::Devices::FW1814
