#pragma once

#include "Id.hpp"
#include "Port.hpp"

#include <compare>
#include <string>
#include <vector>

namespace ASFW::AudioModel {

using ChannelId = Id<struct ChannelTag>;

struct Channel {
    ChannelId id;
    std::string name;
    std::vector<PortId> ports;

    auto operator<=>(const Channel&) const = default;
};

} // namespace ASFW::AudioModel
