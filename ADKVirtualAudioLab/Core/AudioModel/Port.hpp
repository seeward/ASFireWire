#pragma once

#include "Id.hpp"

#include <cstdint>
#include <string>

namespace ASFW::AudioModel {

enum class PortDirection {
    Input,
    Output,
};

struct Port {
    PortId id;
    NodeId owner;

    PortDirection direction;

    // Port is already the atomic routing unit.
    uint32_t channels{1};

    std::string name;
};

} // namespace ASFW::AudioModel
