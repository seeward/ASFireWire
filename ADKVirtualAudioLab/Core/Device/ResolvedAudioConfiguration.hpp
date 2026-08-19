#pragma once

#include "../AudioModel/Topology.hpp"
#include "Stream.hpp"

namespace ASFW::Device {

struct ResolvedAudioConfiguration {
    AudioModel::Topology topology;
    ResolvedStreamConfiguration streams;
};

} // namespace ASFW::Device
