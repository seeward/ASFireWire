#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Naming.hpp"
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
        auto cfgRes = rt.setConfiguration(DeviceConfiguration{.sampleRate = 32000});
        CHECK(ctx, cfgRes.has_value());
        CHECK(ctx, rt.revision() == 2);
        CHECK(ctx, rt.configuration().sampleRate == 32000);
        CHECK(ctx, rt.resolved().streams.sampleRate == 32000);
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

        // Reject NaN and Inf parameter values
        auto nanParam = rt.setParameter(ParameterId{1}, std::numeric_limits<double>::quiet_NaN());
        CHECK(ctx, !nanParam.has_value());
        CHECK(ctx, nanParam.error().kind == StateErrorKind::InvalidParameterValue);

        auto infParam = rt.setParameter(ParameterId{1}, std::numeric_limits<double>::infinity());
        CHECK(ctx, !infParam.has_value());
        CHECK(ctx, infParam.error().kind == StateErrorKind::InvalidParameterValue);

        // Parameter step quantum validation (Preamp Gain step is 1.0 dB; 45.5 dB should be rejected)
        auto unalignedParam = rt.setParameter(ParameterId{1}, 45.5);
        CHECK(ctx, !unalignedParam.has_value());
        CHECK(ctx, unalignedParam.error().kind == StateErrorKind::InvalidParameterValue);

        // Routing mutation: Output Mux switch to Mixer Out (Bundle 2)
        std::array<RouteBundleId, 1> bundle2 = {RouteBundleId{2}};
        auto routeRes = rt.setActiveRouteBundles(NodeId{6}, bundle2);
        CHECK(ctx, routeRes.has_value());
        CHECK(ctx, rt.state().routers.at(NodeId{6}).activeBundles.size() == 1);
        CHECK(ctx, rt.state().routers.at(NodeId{6}).activeBundles.front() == RouteBundleId{2});

        // Reject duplicate active bundle IDs
        std::array<RouteBundleId, 2> dupBundles = {RouteBundleId{1}, RouteBundleId{1}};
        auto dupRoute = rt.setActiveRouteBundles(NodeId{2}, dupBundles);
        CHECK(ctx, !dupRoute.has_value());
        CHECK(ctx, dupRoute.error().kind == StateErrorKind::DuplicateActiveRouteBundle);

        // Routing constraint violation: OutMux maxActiveBundles is 1, trying to activate both bundles 1 and 2
        std::array<RouteBundleId, 2> bundles12 = {RouteBundleId{1}, RouteBundleId{2}};
        auto badRoute = rt.setActiveRouteBundles(NodeId{6}, bundles12);
        CHECK(ctx, !badRoute.has_value());
        CHECK(ctx, badRoute.error().kind == StateErrorKind::RoutingConstraintViolated);

        // Complete state snapshot verification:
        // Sparse state rejected
        DeviceState sparseState;
        sparseState.topologyRevision = rt.resolved().topology.revision;
        auto sparseVal = validateState(rt.resolved().topology, sparseState);
        CHECK(ctx, !sparseVal.has_value());
        CHECK(ctx, sparseVal.error().kind == StateErrorKind::MissingParameter);

        // Complete state with missing router rejected
        auto completeWithoutRouter = rt.state();
        completeWithoutRouter.routers.erase(NodeId{2});
        auto noRouterVal = validateState(rt.resolved().topology, completeWithoutRouter);
        CHECK(ctx, !noRouterVal.has_value());
        CHECK(ctx, noRouterVal.error().kind == StateErrorKind::MissingRouter);

        // Complete state with extra parameter rejected
        auto completeWithExtra = rt.state();
        completeWithExtra.parameters[ParameterId{9999}] = 0.0;
        auto extraParamVal = validateState(rt.resolved().topology, completeWithExtra);
        CHECK(ctx, !extraParamVal.has_value());
        CHECK(ctx, extraParamVal.error().kind == StateErrorKind::NonexistentParameter);

        // Meter mutation validation & state invariant preservation
        // Reject NaN meter value
        auto nanMeter = rt.updateMeter(MeterId{1}, std::numeric_limits<double>::quiet_NaN());
        CHECK(ctx, !nanMeter.has_value());
        CHECK(ctx, nanMeter.error().kind == StateErrorKind::InvalidMeterValue);
        CHECK(ctx, rt.state().meters.at(MeterId{1}) == -96.0); // unchanged

        // Reject out-of-bounds meter value (max is 0.0 dB)
        auto badMeter = rt.updateMeter(MeterId{1}, 10.0);
        CHECK(ctx, !badMeter.has_value());
        CHECK(ctx, badMeter.error().kind == StateErrorKind::InvalidMeterValue);
        CHECK(ctx, rt.state().meters.at(MeterId{1}) == -96.0); // unchanged

        // Valid meter update
        auto goodMeter = rt.updateMeter(MeterId{1}, -12.5);
        CHECK(ctx, goodMeter.has_value());
        CHECK(ctx, rt.state().meters.at(MeterId{1}) == -12.5);
        CHECK(ctx, validateState(rt.resolved().topology, rt.state()).has_value());
    }

    // 4. FW1814 configuration cascade: rate and optical mode both rebuild the
    // topology, the stream plan and the state together, under one revision.
    {
        auto rtRes = VirtualDeviceRuntime::create(VirtualDeviceKind::FW1814);
        REQUIRE(ctx, rtRes.has_value());
        auto& rt = *rtRes;

        struct Step {
            uint32_t rate;
            OpticalMode in;
            OpticalMode out;
            uint32_t capture;
            uint32_t playback;
        };
        // Counts from Linux bebob_maudio.c:228-241.
        const Step steps[] = {
            {48000, OpticalMode::Adat,  OpticalMode::Adat,  16, 12},
            {96000, OpticalMode::Adat,  OpticalMode::Adat,  12,  8},
            {96000, OpticalMode::Spdif, OpticalMode::Adat,  10,  8},
            {44100, OpticalMode::Spdif, OpticalMode::Spdif, 10,  6},
            {48000, OpticalMode::Adat,  OpticalMode::Adat,  16, 12},
        };

        uint64_t previousRevision = rt.revision();
        for (const auto& step : steps) {
            auto applied = rt.setConfiguration(DeviceConfiguration{
                .sampleRate = step.rate,
                .opticalInput = step.in,
                .opticalOutput = step.out,
            });
            CHECK(ctx, applied.has_value());

            CHECK(ctx, rt.revision() == previousRevision + 1);
            previousRevision = rt.revision();

            // Everything the revision covers must agree: stream plan, topology
            // and state all describe the mode just committed.
            CHECK_EQ_U32(ctx, rt.resolved().streams.streams[0].channels, step.capture);
            CHECK_EQ_U32(ctx, rt.resolved().streams.streams[1].channels, step.playback);
            CHECK_EQ_U32(ctx, rt.resolved().streams.sampleRate, step.rate);
            CHECK(ctx, rt.resolved().topology.revision == rt.revision());
            CHECK(ctx, rt.state().topologyRevision == rt.revision());
            CHECK(ctx, validate(rt.resolved().topology).has_value());
            CHECK(ctx, validateState(rt.resolved().topology, rt.state()).has_value());

            // Host capture ports track the stream plan exactly.
            uint32_t captureChannels = 0;
            for (const auto& node : rt.resolved().topology.nodes) {
                auto* endpoint = std::get_if<EndpointNode>(&node.body);
                if (endpoint == nullptr || endpoint->kind != EndpointKind::Host) continue;
                for (const auto& port : rt.resolved().topology.ports) {
                    if (port.owner == node.id && port.direction == PortDirection::Input) {
                        captureChannels += port.channels;
                    }
                }
            }
            CHECK_EQ_U32(ctx, captureChannels, step.capture);
        }

        // A rate the device does not support leaves the committed revision alone.
        const uint64_t before = rt.revision();
        auto rejected = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 192000,
            .opticalInput = OpticalMode::Adat,
            .opticalOutput = OpticalMode::Adat,
        });
        CHECK(ctx, !rejected.has_value());
        CHECK(ctx, rt.revision() == before);
        CHECK_EQ_U32(ctx, rt.resolved().streams.streams[0].channels, 16);
    }

    // 4. Saffire Dynamic Optical Configuration
    {
        auto rtRes = VirtualDeviceRuntime::create(VirtualDeviceKind::SaffirePro24DSP);
        REQUIRE(ctx, rtRes.has_value());
        auto& rt = *rtRes;

        // Default is ADAT mode: 6 Analog + 2 SPDIF + 8 ADAT = 16 physical in ports
        CHECK(ctx, displayName(rt.resolved().topology.ports[15]) == "ADAT In 8");

        // Switch to S/PDIF optical mode
        auto optRes = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Spdif,
            .opticalOutput = OpticalMode::Spdif,
        });
        CHECK(ctx, optRes.has_value());
        CHECK(ctx, rt.revision() == 2);
        CHECK(ctx, displayName(rt.resolved().topology.ports[9]) == "Opt S/PDIF In 2");
    }
}

} // namespace ASFW::LabTests
