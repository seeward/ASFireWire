#include "../TestHarness.hpp"
#include "../../Runtime/VirtualDeviceRegistry.hpp"
#include "../../Runtime/VirtualDeviceRuntime.hpp"

#include <array>
#include <vector>

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;
using namespace ASFW::Runtime;

void RunVirtualDeviceRuntimeTests(TestContext& ctx) {
    // 1. Registry tests
    auto defs = virtualDevices();
    CHECK(ctx, defs.size() == 4);

    CHECK(ctx, findVirtualDevice(VirtualDeviceKind::Duet) != nullptr);
    CHECK(ctx, findVirtualDevice(VirtualDeviceKind::Phase88) != nullptr);
    CHECK(ctx, findVirtualDevice(VirtualDeviceKind::FW1814) != nullptr);
    CHECK(ctx, findVirtualDevice(VirtualDeviceKind::SaffirePro24DSP) != nullptr);

    // 2. Lifecycle and default configuration for all 4 devices
    for (const auto& def : defs) {
        auto rtRes = VirtualDeviceRuntime::create(def.kind);
        CHECK(ctx, rtRes.has_value());
        if (!rtRes.has_value()) {
            std::printf("Failed to create VirtualDeviceRuntime for kind %d: %s\n",
                        static_cast<int>(def.kind), rtRes.error().message.c_str());
            continue;
        }

        const auto& rt = *rtRes;
        CHECK(ctx, rt.revision() == 1);
        CHECK(ctx, !rt.capabilities().sampleRates.empty());
        CHECK(ctx, !rt.capabilities().identity.manufacturer.empty());
        CHECK(ctx, !rt.capabilities().identity.model.empty());
        CHECK(ctx, !rt.resolved().streams.streams.empty());
        CHECK(ctx, !rt.resolved().topology.nodes.empty());
        CHECK(ctx, rt.state().topologyRevision == 1);
    }

    // 3. Duet Configuration & Mutations
    {
        auto rtRes = VirtualDeviceRuntime::create(VirtualDeviceKind::Duet);
        REQUIRE(ctx, rtRes.has_value());
        auto& rt = *rtRes;

        // Valid sample rate switch
        auto cfgRes = rt.setConfiguration(DeviceConfiguration{.sampleRate = 96000});
        CHECK(ctx, cfgRes.has_value());
        CHECK(ctx, rt.revision() == 2);
        CHECK(ctx, rt.configuration().sampleRate == 96000);
        CHECK(ctx, rt.resolved().streams.sampleRate == 96000);
        CHECK(ctx, rt.state().topologyRevision == 2);

        // Unsupported sample rate rejected cleanly without bumping revision
        auto badCfg = rt.setConfiguration(DeviceConfiguration{.sampleRate = 192000});
        CHECK(ctx, !badCfg.has_value());
        CHECK(ctx, rt.revision() == 2);

        // Parameter mutation with domain checking
        auto paramRes = rt.setParameter(ParameterId{1}, 45.0); // Preamp 1 Gain
        CHECK(ctx, paramRes.has_value());
        CHECK(ctx, std::get<double>(rt.state().parameters.at(ParameterId{1})) == 45.0);

        // Out-of-bounds parameter rejected
        auto badParam = rt.setParameter(ParameterId{1}, 100.0); // max is 75.0
        CHECK(ctx, !badParam.has_value());
        CHECK(ctx, badParam.error().kind == StateErrorKind::InvalidParameterValue);

        // Routing mutation: Output Mux switch to Mixer Out (Bundle 2)
        std::array<RouteBundleId, 1> bundle2 = {RouteBundleId{2}};
        auto routeRes = rt.setActiveRouteBundles(NodeId{6}, bundle2);
        CHECK(ctx, routeRes.has_value());
        CHECK(ctx, rt.state().routers.at(NodeId{6}).activeBundles.size() == 1);
        CHECK(ctx, rt.state().routers.at(NodeId{6}).activeBundles.front() == RouteBundleId{2});

        // Routing constraint violation: OutMux maxActiveBundles is 1, trying to activate both bundles 1 and 2
        std::array<RouteBundleId, 2> bundles12 = {RouteBundleId{1}, RouteBundleId{2}};
        auto badRoute = rt.setActiveRouteBundles(NodeId{6}, bundles12);
        CHECK(ctx, !badRoute.has_value());
        CHECK(ctx, badRoute.error().kind == StateErrorKind::RoutingConstraintViolated);
    }

    // 4. Saffire Dynamic Optical Configuration
    {
        auto rtRes = VirtualDeviceRuntime::create(VirtualDeviceKind::SaffirePro24DSP);
        REQUIRE(ctx, rtRes.has_value());
        auto& rt = *rtRes;

        // Default is ADAT mode: 6 Analog + 2 SPDIF + 8 ADAT = 16 physical in ports
        CHECK(ctx, rt.resolved().topology.ports[15].name == "Phys In: ADAT 8");

        // Switch to S/PDIF optical mode
        auto optRes = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Spdif,
            .opticalOutput = OpticalMode::Spdif,
        });
        CHECK(ctx, optRes.has_value());
        CHECK(ctx, rt.revision() == 2);
        CHECK(ctx, rt.resolved().topology.ports[9].name == "Phys In: Opt SPDIF 2");
    }
}

} // namespace ASFW::LabTests
