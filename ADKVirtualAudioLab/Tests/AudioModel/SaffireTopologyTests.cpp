#include "../TestHarness.hpp"
#include "../../Devices/SaffirePro24DSP/Resolve.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

void RunSaffireTopologyTests(TestContext& ctx) {
    auto resolvedRes = Devices::SaffirePro24DSP::resolve(DeviceConfiguration{.sampleRate = 48000});
    REQUIRE(ctx, resolvedRes.has_value());
    const auto& saffire = resolvedRes->topology;

    auto result = validate(saffire);
    CHECK(ctx, result.has_value());

    if (!result.has_value()) {
        std::printf("Saffire validation failed: %s\n", result.error().message.c_str());
    }

    // Verify 46x46 DICE Router Crossbar
    auto* diceRouter = std::get_if<RouterNode>(&saffire.nodes[2].body);
    REQUIRE(ctx, diceRouter != nullptr);
    CHECK(ctx, diceRouter->inputs.size() == 46);
    CHECK(ctx, diceRouter->outputs.size() == 46);
    CHECK(ctx, diceRouter->legalBundles.size() == 46 * 46);
    CHECK(ctx, diceRouter->constraints.maxActiveRoutes == 128);

    // Verify 18x16 Hardware Mixer
    auto* mixer = std::get_if<MixerNode>(&saffire.nodes[3].body);
    REQUIRE(ctx, mixer != nullptr);
    CHECK(ctx, mixer->inputs.size() == 18);
    CHECK(ctx, mixer->outputs.size() == 16);
    CHECK(ctx, mixer->crosspoints.size() == 18 * 16);

    // Verify Processors
    auto* outGroup = std::get_if<ProcessorNode>(&saffire.nodes[4].body);
    REQUIRE(ctx, outGroup != nullptr);
    CHECK(ctx, outGroup->inputs.size() == 6);
    CHECK(ctx, outGroup->outputs.size() == 10);

    // Invariant negative tests
    {
        // 1. Processor with missing input port in ports table
        auto invalid = saffire;
        auto* proc = std::get_if<ProcessorNode>(&invalid.nodes[5].body);
        REQUIRE(ctx, proc != nullptr);
        proc->inputs.push_back(PortId{9999}); // nonexistent port
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::NonexistentPort);
        }
    }

    {
        // 2. Meter targeting nonexistent node
        auto invalid = saffire;
        invalid.meters.push_back(Meter{
            MeterId{99},
            NodeId{9999},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Ghost Meter",
        });
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::NonexistentNode);
        }
    }
}

} // namespace ASFW::LabTests
