#pragma once

#include "Id.hpp"
#include "Port.hpp"

#include <compare>
#include <string>
#include <vector>

namespace ASFW::AudioModel {

using BusId = Id<struct BusTag>;

enum class BusSemantic {
    Unknown,
    Main,
    Aux,
    Monitor,
    Cue,
};

struct Bus {
    BusId id;
    BusSemantic semantic{BusSemantic::Unknown};
    std::string name;
    std::vector<PortId> ports;

    auto operator<=>(const Bus&) const = default;
};

} // namespace ASFW::AudioModel
