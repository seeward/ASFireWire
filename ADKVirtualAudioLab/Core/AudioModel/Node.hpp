#pragma once

#include "Id.hpp"
#include "Mixer.hpp"
#include "Processor.hpp"
#include "Router.hpp"

#include <compare>
#include <string>
#include <variant>
#include <vector>

namespace ASFW::AudioModel {

enum class EndpointKind {
    Physical,
    Host,
};

struct EndpointNode {
    EndpointKind kind{EndpointKind::Physical};

    constexpr auto operator<=>(const EndpointNode&) const = default;
};

struct RouterNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
    std::vector<RouteBundle> legalBundles;
    RouterConstraints constraints;
};

struct MixerNode {
    std::vector<PortId> inputs;
    std::vector<PortId> outputs;
    std::vector<MixerCrosspoint> crosspoints;
};

using NodeBody = std::variant<
    EndpointNode,
    RouterNode,
    MixerNode,
    ProcessorNode
>;

struct Node {
    NodeId id;
    std::string name;
    NodeBody body{EndpointNode{}};
};

} // namespace ASFW::AudioModel
