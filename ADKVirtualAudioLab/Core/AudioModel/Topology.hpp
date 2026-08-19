#pragma once

#include "Bus.hpp"
#include "Channel.hpp"
#include "Id.hpp"
#include "Link.hpp"
#include "Meter.hpp"
#include "Mixer.hpp"
#include "Node.hpp"
#include "Parameter.hpp"
#include "Port.hpp"
#include "Processor.hpp"
#include "Router.hpp"

#include <cstdint>
#include <vector>

namespace ASFW::AudioModel {

struct Topology {
    uint64_t revision{};

    std::vector<Node> nodes;
    std::vector<Port> ports;
    std::vector<FixedLink> fixedLinks;
    std::vector<Parameter> parameters;
    std::vector<Meter> meters;

    std::vector<Channel> channels;
    std::vector<Bus> buses;
};

} // namespace ASFW::AudioModel
