#pragma once

#include "Topology.hpp"

#include <expected>
#include <string>
#include <vector>

namespace ASFW::AudioModel {

enum class TopologyErrorKind {
    DuplicateId,
    NonexistentNode,
    NonexistentPort,
    NonexistentCrosspoint,
    InvalidPortDirection,
    InvalidChannelCount,
    IncompatibleChannelCount,
    MultipleDriversOnInput,
    ForeignPortReference,
    DuplicateRouteOrCrosspoint,
    InvalidDomain,
    InvalidConstraint,
};

struct TopologyError {
    TopologyErrorKind kind;
    std::string message;
};

std::vector<TopologyError> validateAll(const Topology& topology);
std::expected<void, TopologyError> validate(const Topology& topology);

} // namespace ASFW::AudioModel
