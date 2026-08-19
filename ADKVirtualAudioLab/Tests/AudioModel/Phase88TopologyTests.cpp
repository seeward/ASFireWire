#include "../TestHarness.hpp"
#include "../../Devices/Phase88/Resolve.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

void RunPhase88TopologyTests(TestContext& ctx) {
    auto resolvedRes = Devices::Phase88::resolve(DeviceConfiguration{.sampleRate = 48000});
    REQUIRE(ctx, resolvedRes.has_value());
    const auto& phase88 = resolvedRes->topology;

    auto result = validate(phase88);
    CHECK(ctx, result.has_value());

    if (!result.has_value()) {
        std::printf("Phase88 validation failed: %s\n", result.error().message.c_str());
    }

    // Verify 12x2 Mixer
    auto* mixer = std::get_if<MixerNode>(&phase88.nodes[3].body);
    REQUIRE(ctx, mixer != nullptr);
    CHECK(ctx, mixer->inputs.size() == 12);
    CHECK(ctx, mixer->outputs.size() == 2);
    CHECK(ctx, mixer->crosspoints.size() == 24);

    // Verify Pre-mixer Stream Source Selector Router
    auto* srcMux = std::get_if<RouterNode>(&phase88.nodes[2].body);
    REQUIRE(ctx, srcMux != nullptr);
    CHECK(ctx, srcMux->legalBundles.size() == 5);

    // Verify Output Selector Router (5 output pairs x 7 sources = 35 legal bundles)
    auto* outMux = std::get_if<RouterNode>(&phase88.nodes[4].body);
    REQUIRE(ctx, outMux != nullptr);
    CHECK(ctx, outMux->legalBundles.size() == 35);
    CHECK(ctx, outMux->constraints.maxDestinationsPerInput == 5);

    // Invariant negative tests
    {
        // 1. Channel count mismatch between fixed link endpoints
        auto invalid = phase88;
        invalid.ports[0].channels = 2; // Port 1 has 2 channels, Port 21 has 1 channel
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::IncompatibleChannelCount);
        }
    }

    {
        // 2. Zero channel count on port
        auto invalid = phase88;
        invalid.ports[0].channels = 0;
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::InvalidChannelCount);
        }
    }

    {
        // 3. Duplicate Crosspoint in Mixer
        auto invalid = phase88;
        auto* mixerNode = std::get_if<MixerNode>(&invalid.nodes[3].body);
        REQUIRE(ctx, mixerNode != nullptr);
        mixerNode->crosspoints.push_back(
            MixerCrosspoint{CrosspointId{999}, PortId{61}, PortId{75}}
        );
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::DuplicateRouteOrCrosspoint);
        }
    }
}

} // namespace ASFW::LabTests
