#pragma once

#include "Id.hpp"

#include <compare>

namespace ASFW::AudioModel {

struct FixedLink {
    PortId source;
    PortId destination;

    constexpr auto operator<=>(const FixedLink&) const = default;
};

} // namespace ASFW::AudioModel
