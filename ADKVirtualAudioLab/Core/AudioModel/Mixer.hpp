#pragma once

#include "Id.hpp"

#include <compare>

namespace ASFW::AudioModel {

struct MixerCrosspoint {
    CrosspointId id;
    PortId input;
    PortId output;

    constexpr auto operator<=>(const MixerCrosspoint&) const = default;
};

} // namespace ASFW::AudioModel
