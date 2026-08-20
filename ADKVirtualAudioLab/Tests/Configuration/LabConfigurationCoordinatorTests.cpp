#include "../TestHarness.hpp"
#include "../../Lab/LabConfigurationCoordinator.hpp"

#include <variant>

namespace ASFW::LabTests {
namespace {

using namespace ASFW::Configuration;

::ASFW::Device::DeviceConfiguration Config(uint32_t rate) {
    return ::ASFW::Device::DeviceConfiguration{
        .sampleRate = rate,
        .opticalInput = ::ASFW::Device::OpticalMode::Adat,
        .opticalOutput = ::ASFW::Device::OpticalMode::Adat,
    };
}

ConfigurationIdentity Pending(const ASFW::Lab::LabConfigurationCoordinator& coordinator) {
    const auto snapshot = coordinator.Snapshot();
    return snapshot.pendingIdentity.value_or(ConfigurationIdentity{});
}

} // namespace

void RunLabConfigurationCoordinatorTests(TestContext& ctx) {
    ASFW::Lab::LabConfigurationCoordinator coordinator{0x88, 1, Config(48000)};

    auto step = coordinator.Dispatch(ConfigurationEvent{ControlIntent{
        .endpointId = 0x88,
        .routeGeneration = 1,
        .requested = Config(44100),
    }});
    REQUIRE(ctx, step.has_value());
    const auto identity = Pending(coordinator);

    step = coordinator.Dispatch(ConfigurationEvent{CandidateAccepted{
        .identity = identity,
        .candidate = Config(44100),
    }});
    REQUIRE(ctx, step.has_value());
    CHECK(ctx, std::holds_alternative<RequestADKWindowEffect>(step->effects[0]));

    step = coordinator.Dispatch(ConfigurationEvent{ADKPerformGranted{.identity = identity}});
    REQUIRE(ctx, step.has_value());
    REQUIRE(ctx, std::holds_alternative<ApplyHardwareEffect>(step->effects[0]));
    const auto outcome = coordinator.CompleteHardware(
        std::get<ApplyHardwareEffect>(step->effects[0]));
    CHECK(ctx, HardwareKind(outcome) == HardwareOutcomeKind::ConfirmedRequested);

    // Scripts are one-shot: a known reject reports unchanged once, then the
    // normal confirmed path is restored for the next transaction.
    coordinator.SetNextHardwareOutcome(ASFW::Lab::ScriptedHardwareOutcome::Unchanged);
    const auto rejected = coordinator.CompleteHardware(
        std::get<ApplyHardwareEffect>(step->effects[0]));
    CHECK(ctx, HardwareKind(rejected) == HardwareOutcomeKind::Unchanged);
    const auto restored = coordinator.CompleteHardware(
        std::get<ApplyHardwareEffect>(step->effects[0]));
    CHECK(ctx, HardwareKind(restored) == HardwareOutcomeKind::ConfirmedRequested);

    // An observation is already hardware-confirmed. It receives an ADK
    // perform window and then projects directly, without consuming a script
    // or producing an ApplyHardware effect.
    ASFW::Lab::LabConfigurationCoordinator observed{0x99, 1, Config(48000)};
    step = observed.Dispatch(ConfigurationEvent{HardwareObserved{
        .endpointId = 0x99,
        .routeGeneration = 1,
        .confirmed = ConfirmedHardwareConfiguration{.configuration = Config(44100)},
    }});
    REQUIRE(ctx, step.has_value());
    REQUIRE(ctx, step->effects.size() == 1);
    CHECK(ctx, std::holds_alternative<RequestADKWindowEffect>(step->effects[0]));
    const auto observationIdentity = Pending(observed);

    step = observed.Dispatch(ConfigurationEvent{ADKPerformGranted{
        .identity = observationIdentity,
    }});
    REQUIRE(ctx, step.has_value());
    REQUIRE(ctx, step->effects.size() == 1);
    CHECK(ctx, std::holds_alternative<ProjectADKEffect>(step->effects[0]));
}

} // namespace ASFW::LabTests
