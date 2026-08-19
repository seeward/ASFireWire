#include "VirtualDeviceRuntime.hpp"

#include <algorithm>

namespace ASFW::Runtime {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

std::expected<VirtualDeviceRuntime, ResolveError> VirtualDeviceRuntime::create(
    VirtualDeviceKind kind) {

    const auto* def = findVirtualDevice(kind);
    if (def == nullptr) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Unknown VirtualDeviceKind"
        });
    }

    return create(kind, def->defaultConfiguration());
}

std::expected<VirtualDeviceRuntime, ResolveError> VirtualDeviceRuntime::create(
    VirtualDeviceKind kind,
    const DeviceConfiguration& config) {

    const auto* def = findVirtualDevice(kind);
    if (def == nullptr) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Unknown VirtualDeviceKind"
        });
    }

    auto resolved = def->resolve(config);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    // Set initial revision = 1
    const uint64_t initialRevision = 1;
    resolved->topology.revision = initialRevision;

    // Validate topology
    auto topVal = validate(resolved->topology);
    if (!topVal.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Resolved topology failed validation: " + topVal.error().message
        });
    }

    // Generate and validate initial state
    auto state = def->makeInitialState(*resolved);
    state.topologyRevision = initialRevision;

    auto stateVal = validateState(resolved->topology, state);
    if (!stateVal.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Initial state failed validation: " + stateVal.error().message
        });
    }

    VirtualDeviceRuntime runtime(def);
    runtime.revision_ = initialRevision;
    runtime.configuration_ = config;
    runtime.resolved_ = std::move(*resolved);
    runtime.state_ = std::move(state);

    return runtime;
}

std::expected<void, ResolveError> VirtualDeviceRuntime::setConfiguration(
    const DeviceConfiguration& config) {

    auto nextResolved = definition_->resolve(config);
    if (!nextResolved.has_value()) {
        return std::unexpected(nextResolved.error());
    }

    const uint64_t nextRevision = revision_ + 1;
    nextResolved->topology.revision = nextRevision;

    auto topVal = validate(nextResolved->topology);
    if (!topVal.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Resolved topology failed validation: " + topVal.error().message
        });
    }

    auto nextState = definition_->makeInitialState(*nextResolved);
    nextState.topologyRevision = nextRevision;

    auto stateVal = validateState(nextResolved->topology, nextState);
    if (!stateVal.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Initial state failed validation: " + stateVal.error().message
        });
    }

    // All validation succeeded: commit transaction atomically
    configuration_ = config;
    resolved_ = std::move(*nextResolved);
    state_ = std::move(nextState);
    revision_ = nextRevision;

    notifyChange();
    return {};
}

std::expected<void, StateError> VirtualDeviceRuntime::setParameter(
    ParameterId id,
    const ParameterValue& value) {

    // Validate proposed parameter value
    auto res = validateParameterValue(resolved_.topology, id, value);
    if (!res.has_value()) {
        return res;
    }

    state_.parameters[id] = value;
    notifyChange();
    return {};
}

std::expected<void, StateError> VirtualDeviceRuntime::setActiveRouteBundles(
    NodeId routerNode,
    std::span<const RouteBundleId> bundles) {

    RouterState proposedState{
        .activeBundles = std::vector<RouteBundleId>(bundles.begin(), bundles.end()),
    };

    auto res = validateRouterState(resolved_.topology, routerNode, proposedState);
    if (!res.has_value()) {
        return res;
    }

    state_.routers[routerNode] = std::move(proposedState);
    notifyChange();
    return {};
}

std::expected<void, StateError> VirtualDeviceRuntime::activateRouteBundle(
    NodeId routerNode,
    RouteBundleId bundleId) {

    std::vector<RouteBundleId> currentBundles;
    if (auto it = state_.routers.find(routerNode); it != state_.routers.end()) {
        currentBundles = it->second.activeBundles;
    }
    if (std::find(currentBundles.begin(), currentBundles.end(), bundleId) == currentBundles.end()) {
        currentBundles.push_back(bundleId);
    }

    return setActiveRouteBundles(routerNode, currentBundles);
}

std::expected<void, StateError> VirtualDeviceRuntime::deactivateRouteBundle(
    NodeId routerNode,
    RouteBundleId bundleId) {

    std::vector<RouteBundleId> currentBundles;
    if (auto it = state_.routers.find(routerNode); it != state_.routers.end()) {
        currentBundles = it->second.activeBundles;
    }
    currentBundles.erase(
        std::remove(currentBundles.begin(), currentBundles.end(), bundleId),
        currentBundles.end()
    );

    return setActiveRouteBundles(routerNode, currentBundles);
}

std::expected<void, StateError> VirtualDeviceRuntime::clearRouteBundles(
    NodeId routerNode) {

    return setActiveRouteBundles(routerNode, {});
}

std::expected<void, StateError> VirtualDeviceRuntime::updateMeter(
    MeterId id,
    double value) {

    auto res = validateMeterValue(resolved_.topology, id, value);
    if (!res.has_value()) {
        return res;
    }

    state_.meters[id] = value;
    notifyChange();
    return {};
}

} // namespace ASFW::Runtime
