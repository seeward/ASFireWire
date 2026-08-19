#include "../TestHarness.hpp"
#include "../../Devices/Duet/Resolve.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

void RunDuetTopologyTests(TestContext& ctx) {
    auto resolvedRes = Devices::Duet::resolve(DeviceConfiguration{.sampleRate = 48000});
    REQUIRE(ctx, resolvedRes.has_value());
    const auto& duet = resolvedRes->topology;

    auto result = validate(duet);
    CHECK(ctx, result.has_value());

    if (!result.has_value()) {
        std::printf("Duet validation failed with error: %s\n", result.error().message.c_str());
    }

    // Verify independent input bundles
    auto* inMux = std::get_if<RouterNode>(&duet.nodes[1].body);
    REQUIRE(ctx, inMux != nullptr);
    CHECK(ctx, inMux->legalBundles.size() == 4);
    for (const auto& b : inMux->legalBundles) {
        CHECK(ctx, b.routes.size() == 1);
    }

    // Verify coupled output bundles
    auto* outMux = std::get_if<RouterNode>(&duet.nodes[5].body);
    REQUIRE(ctx, outMux != nullptr);
    CHECK(ctx, outMux->legalBundles.size() == 2);
    for (const auto& b : outMux->legalBundles) {
        CHECK(ctx, b.routes.size() == 2);
    }

    // Invariant negative tests
    {
        // 1. Duplicate NodeId
        auto invalid = duet;
        invalid.nodes.push_back(Node{NodeId{1}, "Clashing Node", EndpointNode{EndpointKind::Physical}});
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::DuplicateId);
        }
    }

    {
        // 2. Fixed link destination direction wrong (connecting to an Output)
        auto invalid = duet;
        invalid.fixedLinks.push_back(FixedLink{PortId{1}, PortId{3}}); // 3 is Output
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::InvalidPortDirection);
        }
    }

    {
        // 3. Legal route referencing foreign port
        auto invalid = duet;
        auto* router = std::get_if<RouterNode>(&invalid.nodes[1].body);
        REQUIRE(ctx, router != nullptr);
        router->legalBundles.push_back(RouteBundle{
            RouteBundleId{99},
            {Route{PortId{11}, PortId{55}}}, // 55 belongs to OutMux
        });
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::ForeignPortReference);
        }
    }

    {
        // 4. Duplicate RouteBundleId within router
        auto invalid = duet;
        auto* router = std::get_if<RouterNode>(&invalid.nodes[1].body);
        REQUIRE(ctx, router != nullptr);
        router->legalBundles.push_back(RouteBundle{
            RouteBundleId{1},
            {Route{PortId{11}, PortId{15}}},
        });
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::DuplicateId);
        }
    }
}

} // namespace ASFW::LabTests
