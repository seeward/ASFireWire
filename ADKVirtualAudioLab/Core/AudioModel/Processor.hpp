#pragma once

#include "Id.hpp"

#include <vector>

namespace ASFW::AudioModel {

struct ProcessorNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
};

} // namespace ASFW::AudioModel
