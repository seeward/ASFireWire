#include "LabConfigurationCoordinator.hpp"

#include <type_traits>
#include <variant>

namespace ASFW::Lab {
namespace {

std::optional<Configuration::ConfigurationIdentity>
PendingIdentity(const Configuration::DeviceConfigurationState& state) noexcept
{
    return std::visit([](const auto& value)
        -> std::optional<Configuration::ConfigurationIdentity> {
        using State = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<State, Configuration::AwaitingCandidate>) {
            return value.identity;
        } else if constexpr (std::is_same_v<State, Configuration::AwaitingADKPerform> ||
                             std::is_same_v<State, Configuration::AwaitingHardware> ||
                             std::is_same_v<State, Configuration::AwaitingADKProjection>) {
            return value.transition.identity;
        }
        return std::nullopt;
    }, state);
}

std::optional<Device::DeviceConfiguration>
PendingConfiguration(const Configuration::DeviceConfigurationState& state) noexcept
{
    return std::visit([](const auto& value)
        -> std::optional<Device::DeviceConfiguration> {
        using State = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<State, Configuration::AwaitingCandidate>) {
            return value.requested;
        } else if constexpr (std::is_same_v<State, Configuration::AwaitingADKPerform> ||
                             std::is_same_v<State, Configuration::AwaitingHardware>) {
            return value.transition.candidate;
        } else if constexpr (std::is_same_v<State, Configuration::AwaitingADKProjection>) {
            return value.plan.confirmed.configuration;
        }
        return std::nullopt;
    }, state);
}

std::optional<Configuration::FailureReason>
LastFailure(const Configuration::DeviceConfigurationState& state) noexcept
{
    return std::visit([](const auto& value)
        -> std::optional<Configuration::FailureReason> {
        using State = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<State, Configuration::Idle>) {
            return value.lastFailure ? std::optional{value.lastFailure->reason}
                                     : std::nullopt;
        } else if constexpr (std::is_same_v<State, Configuration::Recovering> ||
                             std::is_same_v<State, Configuration::Unavailable>) {
            return value.failure.reason;
        }
        return std::nullopt;
    }, state);
}

} // namespace

LabConfigurationCoordinator::LabConfigurationCoordinator(
    Configuration::EndpointId endpointId,
    Configuration::RouteGeneration routeGeneration,
    const Device::DeviceConfiguration& initial) noexcept
    : machine_{
        .state = Configuration::Idle{
            .committed = Configuration::CommittedConfiguration{
                .endpointId = endpointId,
                .routeGeneration = routeGeneration,
                .revision = 1,
                .configuration = initial,
            },
        },
        .nextToken = 1,
    }
{
}

std::expected<Configuration::TransitionResult, Configuration::StateMachineError>
LabConfigurationCoordinator::Dispatch(
    const Configuration::ConfigurationEvent& event) noexcept
{
    auto transition = Configuration::Reduce(machine_, event);
    if (transition) {
        machine_ = transition->next;
    }
    return transition;
}

LabConfigurationSnapshot LabConfigurationCoordinator::Snapshot() const noexcept
{
    LabConfigurationSnapshot snapshot{};
    if (const auto coherent = Configuration::CoherentSnapshot(machine_.state)) {
        snapshot.committed = *coherent;
    }
    snapshot.pendingIdentity = PendingIdentity(machine_.state);
    snapshot.pendingConfiguration = PendingConfiguration(machine_.state);
    snapshot.lastFailure = LastFailure(machine_.state);
    return snapshot;
}

void LabConfigurationCoordinator::SetNextHardwareOutcome(
    ScriptedHardwareOutcome outcome) noexcept
{
    nextHardwareOutcome_ = outcome;
}

Configuration::HardwareConfigurationOutcome
LabConfigurationCoordinator::CompleteHardware(
    const Configuration::ApplyHardwareEffect& effect) noexcept
{
    const auto scripted = nextHardwareOutcome_;
    nextHardwareOutcome_ = ScriptedHardwareOutcome::ConfirmRequested;

    switch (scripted) {
    case ScriptedHardwareOutcome::ConfirmRequested:
        return Configuration::HardwareConfirmedRequested{
            .confirmed = Configuration::ConfirmedHardwareConfiguration{
                .configuration = effect.transition.candidate,
            },
        };
    case ScriptedHardwareOutcome::Unchanged:
        return Configuration::HardwareUnchanged{};
    case ScriptedHardwareOutcome::Unknown:
        return Configuration::HardwareUnknown{};
    }
    return Configuration::HardwareUnknown{};
}

} // namespace ASFW::Lab
