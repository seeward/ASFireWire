#include "../TestHarness.hpp"
#include "../../Devices/FW1814/Resolve.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

void RunFW1814TopologyTests(TestContext& ctx) {
    auto resolvedRes = Devices::FW1814::resolve(DeviceConfiguration{.sampleRate = 48000});
    REQUIRE(ctx, resolvedRes.has_value());
    const auto& fw1814 = resolvedRes->topology;

    auto result = validate(fw1814);
    CHECK(ctx, result.has_value());

    if (!result.has_value()) {
        std::printf("FW1814 validation failed: %s\n", result.error().message.c_str());
    }

    // Verify Main Sum Matrix is a 22x4 MixerNode (11 stereo inputs x 2 stereo outputs = 22 crosspoints)
    auto* sumMixer = std::get_if<MixerNode>(&fw1814.nodes[3].body);
    REQUIRE(ctx, sumMixer != nullptr);
    CHECK(ctx, sumMixer->crosspoints.size() == 22);

    // Verify Aux Downmix Matrix is a 22x2 MixerNode (11 stereo inputs x 1 stereo output = 11 crosspoints)
    auto* auxMixer = std::get_if<MixerNode>(&fw1814.nodes[4].body);
    REQUIRE(ctx, auxMixer != nullptr);
    CHECK(ctx, auxMixer->crosspoints.size() == 11);

    // Verify RouteBundle counts on Headphone Mux and LineOut Mux
    auto* hpMux = std::get_if<RouterNode>(&fw1814.nodes[5].body);
    REQUIRE(ctx, hpMux != nullptr);
    CHECK(ctx, hpMux->legalBundles.size() == 6);

    auto* lineOutMux = std::get_if<RouterNode>(&fw1814.nodes[6].body);
    REQUIRE(ctx, lineOutMux != nullptr);
    CHECK(ctx, lineOutMux->legalBundles.size() == 4);

    // Verify later host playback streams connect directly to digital outputs
    bool hasStreamToSpdifOut = false;
    for (const auto& link : fw1814.fixedLinks) {
        if (link.source == PortId{43} && link.destination == PortId{93}) {
            hasStreamToSpdifOut = true;
            break;
        }
    }
    CHECK(ctx, hasStreamToSpdifOut);

    // Invariant negative tests
    {
        // 1. Parameter with invalid domain (min > max)
        auto invalid = fw1814;
        invalid.parameters[0].domain = ScalarDomain{.min = 10.0, .max = 0.0, .unit = ScalarUnit::Decibels};
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::InvalidDomain);
        }
    }

    {
        // 2. Multiple FixedLinks driving the same destination port
        auto invalid = fw1814;
        invalid.fixedLinks.push_back(FixedLink{PortId{2}, PortId{11}});
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::MultipleDriversOnInput);
        }
    }
}

} // namespace ASFW::LabTests
