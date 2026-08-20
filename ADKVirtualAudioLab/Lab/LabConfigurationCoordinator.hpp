#pragma once

#include "../Core/Configuration/DeviceConfigurationStateMachine.hpp"

#include <expected>
#include <optional>

namespace ASFW::Lab {

// This is deliberately a lab-only hardware port.  It gives the ADK adapter a
// deterministic completion source without pretending to model a FireWire
// transaction.  A script is consumed by the next ApplyHardware effect.
enum class ScriptedHardwareOutcome : uint32_t {
    ConfirmRequested = 0,
    Unchanged = 1,
    Unknown = 2,
};

struct LabConfigurationSnapshot final {
    Configuration::CommittedConfiguration committed{};
    std::optional<Configuration::ConfigurationIdentity> pendingIdentity{};
    std::optional<::ASFW::Device::DeviceConfiguration> pendingConfiguration{};
    std::optional<Configuration::FailureReason> lastFailure{};
};

class LabConfigurationCoordinator final {
public:
    LabConfigurationCoordinator(Configuration::EndpointId endpointId,
                                Configuration::RouteGeneration routeGeneration,
                                const ::ASFW::Device::DeviceConfiguration& initial) noexcept;

    [[nodiscard]] std::expected<Configuration::TransitionResult,
                                Configuration::StateMachineError>
    Dispatch(const Configuration::ConfigurationEvent& event) noexcept;

    [[nodiscard]] LabConfigurationSnapshot Snapshot() const noexcept;

    void SetNextHardwareOutcome(ScriptedHardwareOutcome outcome) noexcept;

    [[nodiscard]] Configuration::HardwareConfigurationOutcome
    CompleteHardware(const Configuration::ApplyHardwareEffect& effect) noexcept;

private:
    Configuration::Machine machine_{};
    ScriptedHardwareOutcome nextHardwareOutcome_{
        ScriptedHardwareOutcome::ConfirmRequested};
};

} // namespace ASFW::Lab
