#pragma once

#include "../AudioModel/Topology.hpp"
#include "../Presentation/DevicePresentation.hpp"
#include "Stream.hpp"

namespace ASFW::Device {

struct ResolvedAudioConfiguration {
    AudioModel::Topology topology;
    ResolvedStreamConfiguration streams;
    Presentation::DevicePresentation presentation;
};

} // namespace ASFW::Device
