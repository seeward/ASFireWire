#pragma once

#include "VirtualDeviceRegistry.hpp"
#include "../Core/AudioModel/Validate.hpp"

#include <functional>
#include <span>

namespace ASFW::Runtime {

class VirtualDeviceRuntime {
public:
    using ChangeCallback = std::function<void(const VirtualDeviceRuntime&)>;

    static std::expected<VirtualDeviceRuntime, Device::ResolveError> create(
        VirtualDeviceKind kind);

    static std::expected<VirtualDeviceRuntime, Device::ResolveError> create(
        VirtualDeviceKind kind,
        const Device::DeviceConfiguration& config);

    VirtualDeviceKind kind() const noexcept { return definition_->kind; }
    uint64_t revision() const noexcept { return revision_; }
    const Device::DeviceCapabilities& capabilities() const noexcept { return definition_->capabilities(); }
    const Device::DeviceConfiguration& configuration() const noexcept { return configuration_; }
    const Device::ResolvedAudioConfiguration& resolved() const noexcept { return resolved_; }
    const AudioModel::DeviceState& state() const noexcept { return state_; }

    // Structural Configuration Mutation (all-or-nothing transactional publication)
    std::expected<void, Device::ResolveError> setConfiguration(const Device::DeviceConfiguration& config);

    // State Mutations (validated through AudioModel::validateState)
    std::expected<void, AudioModel::StateError> setParameter(
        AudioModel::ParameterId id,
        const AudioModel::ParameterValue& value);

    std::expected<void, AudioModel::StateError> setActiveRouteBundles(
        AudioModel::NodeId routerNode,
        std::span<const AudioModel::RouteBundleId> bundles);

    std::expected<void, AudioModel::StateError> activateRouteBundle(
        AudioModel::NodeId routerNode,
        AudioModel::RouteBundleId bundleId);

    std::expected<void, AudioModel::StateError> deactivateRouteBundle(
        AudioModel::NodeId routerNode,
        AudioModel::RouteBundleId bundleId);

    std::expected<void, AudioModel::StateError> clearRouteBundles(
        AudioModel::NodeId routerNode);

    std::expected<void, AudioModel::StateError> updateMeter(
        AudioModel::MeterId id,
        double value);

    void setChangeListener(ChangeCallback callback) {
        changeCallback_ = std::move(callback);
    }

private:
    explicit VirtualDeviceRuntime(const VirtualDeviceDefinition* def)
        : definition_(def) {}

    void notifyChange() {
        if (changeCallback_) {
            changeCallback_(*this);
        }
    }

    const VirtualDeviceDefinition* definition_{nullptr};
    uint64_t revision_{1};
    Device::DeviceConfiguration configuration_{};
    Device::ResolvedAudioConfiguration resolved_{};
    AudioModel::DeviceState state_{};
    ChangeCallback changeCallback_{};
};

} // namespace ASFW::Runtime
