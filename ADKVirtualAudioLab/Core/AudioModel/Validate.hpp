#pragma once

#include "State.hpp"
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
    InvalidChannelCount,
    InvalidPortDirection,
    ForeignPortReference,
    DuplicateRouteOrCrosspoint,
    IncompatibleChannelCount,
    MultipleDriversOnInput,
    InvalidDomain,
    InvalidConstraint,
};

struct TopologyError {
    TopologyErrorKind kind;
    std::string message;
};

enum class StateErrorKind {
    NonexistentParameter,
    InvalidParameterValue,
    NonexistentRouter,
    NonexistentRouteBundle,
    RoutingConstraintViolated,
    NonexistentMeter,
    TopologyRevisionMismatch,
};

struct StateError {
    StateErrorKind kind;
    std::string message;
};

// Structural Topology Validation
std::vector<TopologyError> validateAll(const Topology& topology);
std::expected<void, TopologyError> validate(const Topology& topology);

// Dynamic State Legality Validation
std::expected<void, StateError> validateParameterValue(
    const Topology& topology,
    ParameterId id,
    const ParameterValue& value);

std::expected<void, StateError> validateRouterState(
    const Topology& topology,
    const RouterState& routerState);

std::expected<void, StateError> validateState(
    const Topology& topology,
    const DeviceState& state);

} // namespace ASFW::AudioModel
