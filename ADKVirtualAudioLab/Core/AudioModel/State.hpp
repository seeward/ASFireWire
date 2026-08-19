#pragma once

#include "Id.hpp"
#include "Parameter.hpp"
#include "Router.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ASFW::AudioModel {

struct RouterState {
    NodeId node;
    std::vector<RouteBundleId> activeBundles;
};

struct DeviceState {
    uint64_t topologyRevision{};

    std::unordered_map<NodeId, RouterState> routers;
    std::unordered_map<ParameterId, ParameterValue> parameters;
    std::unordered_map<MeterId, double> meters;
};

} // namespace ASFW::AudioModel
