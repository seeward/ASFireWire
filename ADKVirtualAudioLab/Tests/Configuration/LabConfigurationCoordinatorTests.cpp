#include "../TestHarness.hpp"
#include "../../Lab/LabConfigurationCoordinator.hpp"

#include <variant>

namespace ASFW::LabTests {
namespace {

using namespace ASFW::Configuration;

Device::DeviceConfiguration Config(uint32_t rate) {
    return Device::DeviceConfiguration{
        .sampleRate = rate,
        .opticalInput = Device::OpticalMode::Adat,
        .opticalOutput = Device::OpticalMode::Adat,
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
}

} // namespace ASFW::LabTests
