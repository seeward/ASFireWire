#include "VirtualDeviceRuntime.hpp"

#include "../Core/AudioModel/Naming.hpp"

#include <algorithm>
#include <format>

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


namespace {

std::string opticalText(const std::optional<OpticalMode>& mode) {
    if (!mode.has_value()) return "-";
    return *mode == OpticalMode::Adat ? "ADAT" : "SPDIF";
}

std::string describeParameterValue(const Topology& topology, ParameterId id, const ParameterValue& value) {
    const Parameter* parameter = nullptr;
    for (const auto& candidate : topology.parameters) {
        if (candidate.id == id) parameter = &candidate;
    }

    return std::visit([&](const auto& raw) -> std::string {
        using T = std::decay_t<decltype(raw)>;
        if constexpr (std::is_same_v<T, bool>) {
            return raw ? "on" : "off";
        } else if constexpr (std::is_same_v<T, double>) {
            return std::format("{:.1f}", raw);
        } else {
            // Enums read far better as the item the user picked.
            if (parameter != nullptr) {
                if (const auto* domain = std::get_if<EnumDomain>(&parameter->domain)) {
                    for (const auto& item : domain->values) {
                        if (item.value == raw) return std::format("{} ({})", item.name, raw);
                    }
                }
            }
            return std::format("{}", raw);
        }
    }, value);
}

std::string describeBundles(std::span<const RouteBundleId> bundles) {
    if (bundles.empty()) return "none";
    std::string text;
    for (const auto& bundle : bundles) {
        if (!text.empty()) text += ",";
        text += std::format("{}", bundle.value);
    }
    return text;
}

std::string parameterLabel(const Topology& topology, ParameterId id) {
    for (const auto& parameter : topology.parameters) {
        if (parameter.id == id) return parameter.name;
    }
    return std::format("parameter {}", id.value);
}

std::string nodeLabel(const Topology& topology, NodeId id) {
    for (const auto& node : topology.nodes) {
        if (node.id == id) return node.name;
    }
    return std::format("node {}", id.value);
}

} // namespace

std::string VirtualDeviceRuntime::describeShape() const {
    uint32_t capture = 0;
    uint32_t playback = 0;
    for (const auto& stream : resolved_.streams.streams) {
        (stream.direction == StreamDirection::Capture ? capture : playback) += stream.channels;
    }
    return std::format("{} Hz {}/{} {}in/{}out {}p {}n",
                       resolved_.streams.sampleRate,
                       opticalText(configuration_.opticalInput),
                       opticalText(configuration_.opticalOutput),
                       capture, playback,
                       resolved_.topology.ports.size(),
                       resolved_.topology.nodes.size());
}

std::expected<void, ResolveError> VirtualDeviceRuntime::setConfiguration(
    const DeviceConfiguration& config) {

    const std::string shapeBefore = describeShape();

    auto recordRejection = [&](const std::string& reason) {
        eventLog_.record(LabEvent{
            .kind = LabEventKind::ConfigurationRejected,
            .accepted = false,
            .revision = revision_,
            .label = "configuration",
            .before = shapeBefore,
            .after = shapeBefore,
            .detail = reason,
        });
    };

    auto nextResolved = definition_->resolve(config);
    if (!nextResolved.has_value()) {
        recordRejection(nextResolved.error().message);
        return std::unexpected(nextResolved.error());
    }

    const uint64_t nextRevision = revision_ + 1;
    nextResolved->topology.revision = nextRevision;

    auto topVal = validate(nextResolved->topology);
    if (!topVal.has_value()) {
        recordRejection("topology invalid: " + topVal.error().message);
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Resolved topology failed validation: " + topVal.error().message
        });
    }

    auto nextState = definition_->makeInitialState(*nextResolved);
    nextState.topologyRevision = nextRevision;

    auto stateVal = validateState(nextResolved->topology, nextState);
    if (!stateVal.has_value()) {
        recordRejection("state invalid: " + stateVal.error().message);
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

    eventLog_.record(LabEvent{
        .kind = LabEventKind::ConfigurationCommitted,
        .revision = revision_,
        .label = "configuration",
        .before = shapeBefore,
        .after = describeShape(),
        .detail = "state reset to defaults",
    });

    notifyChange();
    return {};
}

std::expected<void, StateError> VirtualDeviceRuntime::setParameter(
    ParameterId id,
    const ParameterValue& value) {

    const std::string label = parameterLabel(resolved_.topology, id);
    auto existing = state_.parameters.find(id);
    const std::string before = existing == state_.parameters.end()
        ? std::string{"unset"}
        : describeParameterValue(resolved_.topology, id, existing->second);
    const std::string after = describeParameterValue(resolved_.topology, id, value);

    // Validate proposed parameter value
    auto res = validateParameterValue(resolved_.topology, id, value);
    if (!res.has_value()) {
        eventLog_.record(LabEvent{
            .kind = LabEventKind::ParameterRejected,
            .accepted = false,
            .revision = revision_,
            .targetId = id.value,
            .label = label,
            .before = before,
            .after = after,
            .detail = res.error().message,
        });
        return res;
    }

    state_.parameters[id] = value;
    if (before != after) {
        eventLog_.record(LabEvent{
            .kind = LabEventKind::ParameterChanged,
            .revision = revision_,
            .targetId = id.value,
            .label = label,
            .before = before,
            .after = after,
        });
    }
    notifyChange();
    return {};
}

std::expected<void, StateError> VirtualDeviceRuntime::setActiveRouteBundles(
    NodeId routerNode,
    std::span<const RouteBundleId> bundles) {

    RouterState proposedState{
        .activeBundles = std::vector<RouteBundleId>(bundles.begin(), bundles.end()),
    };

    const std::string label = nodeLabel(resolved_.topology, routerNode);
    auto existing = state_.routers.find(routerNode);
    const std::string before = existing == state_.routers.end()
        ? std::string{"none"}
        : describeBundles(existing->second.activeBundles);
    const std::string after = describeBundles(proposedState.activeBundles);

    auto res = validateRouterState(resolved_.topology, routerNode, proposedState);
    if (!res.has_value()) {
        eventLog_.record(LabEvent{
            .kind = LabEventKind::RouteBundlesRejected,
            .accepted = false,
            .revision = revision_,
            .targetId = routerNode.value,
            .label = label,
            .before = before,
            .after = after,
            .detail = res.error().message,
        });
        return res;
    }

    state_.routers[routerNode] = std::move(proposedState);
    if (before != after) {
        eventLog_.record(LabEvent{
            .kind = LabEventKind::RouteBundlesChanged,
            .revision = revision_,
            .targetId = routerNode.value,
            .label = label,
            .before = before,
            .after = after,
        });
    }
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
