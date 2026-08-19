#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Topology.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;

Topology makeVirtualPhase88() {
    Topology t;
    t.revision = 1;

    // Nodes
    const NodeId nPhysIn{1};
    const NodeId nHostIO{2};
    const NodeId nMixerStreamSrcSelector{3};
    const NodeId nMixer{4};
    const NodeId nOutSelector{5};
    const NodeId nPhysOut{6};

    // 1. Pre-Mixer Stream Source Selector (FB 0x07): 5 playback pairs -> 1 selected pair
    std::vector<RouteBundle> mixerStreamSrcBundles;
    for (uint32_t pair = 0; pair < 5; ++pair) {
        mixerStreamSrcBundles.push_back(RouteBundle{
            RouteBundleId{pair + 1},
            {
                Route{PortId{41 + pair * 2}, PortId{51}}, // Stream L -> Selected L
                Route{PortId{42 + pair * 2}, PortId{52}}, // Stream R -> Selected R
            },
        });
    }

    // 2. 12x2 Mixer Crosspoints: 12 inputs (8 analog + 2 SPDIF + 2 selected stream) -> 2 outputs
    std::vector<PortId> mixerInputs;
    for (uint32_t i = 1; i <= 12; ++i) mixerInputs.push_back(PortId{60 + i});
    std::vector<PortId> mixerOutputs = {PortId{75}, PortId{76}};

    std::vector<MixerCrosspoint> mixerCrosspoints;
    uint32_t cpId = 1;
    for (uint32_t i = 1; i <= 12; ++i) {
        mixerCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{60 + i}, PortId{75}}); // In -> Out L
        mixerCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{60 + i}, PortId{76}}); // In -> Out R
    }

    // 3. Playback & Monitor Output Selector (FB 0x06):
    // 5 direct playback pairs + 5 mixer destination choices
    std::vector<PortId> selInputs;
    for (uint32_t i = 1; i <= 10; ++i) selInputs.push_back(PortId{80 + i});
    selInputs.push_back(PortId{91}); // Mixer Out L
    selInputs.push_back(PortId{92}); // Mixer Out R

    std::vector<PortId> selOutputs;
    for (uint32_t i = 1; i <= 10; ++i) selOutputs.push_back(PortId{92 + i});

    std::vector<RouteBundle> outSelBundles;
    uint32_t bundleId = 1;
    // 5 direct stream playback bundles
    for (uint32_t pair = 0; pair < 5; ++pair) {
        outSelBundles.push_back(RouteBundle{
            RouteBundleId{bundleId++},
            {
                Route{PortId{81 + pair * 2}, PortId{93 + pair * 2}},
                Route{PortId{82 + pair * 2}, PortId{94 + pair * 2}},
            },
        });
    }
    // 5 mixer destination bundles (Mixer Out 1/2 can feed any of the 5 stereo physical outputs)
    for (uint32_t pair = 0; pair < 5; ++pair) {
        outSelBundles.push_back(RouteBundle{
            RouteBundleId{bundleId++},
            {
                Route{PortId{91}, PortId{93 + pair * 2}}, // Mixer Out L -> Out Pair L
                Route{PortId{92}, PortId{94 + pair * 2}}, // Mixer Out R -> Out Pair R
            },
        });
    }

    t.nodes = {
        Node{nPhysIn, "Physical Inputs (8 Analog + 2 SPDIF)", EndpointNode{EndpointKind::Physical}},
        Node{nHostIO, "Host Audio Streams (10x10 AMDTP)", EndpointNode{EndpointKind::Host}},
        Node{
            nMixerStreamSrcSelector,
            "Mixer Stream Source Selector (FB 0x07)",
            RouterNode{
                .inputs = {PortId{41}, PortId{42}, PortId{43}, PortId{44}, PortId{45}, PortId{46}, PortId{47}, PortId{48}, PortId{49}, PortId{50}},
                .outputs = {PortId{51}, PortId{52}},
                .legalBundles = std::move(mixerStreamSrcBundles),
                .constraints = RouterConstraints{
                    .maxActiveBundles = 1,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 1,
                },
            },
        },
        Node{
            nMixer,
            "12x2 Hardware Monitor Mixer",
            MixerNode{
                .inputs = std::move(mixerInputs),
                .outputs = std::move(mixerOutputs),
                .crosspoints = std::move(mixerCrosspoints),
            },
        },
        Node{
            nOutSelector,
            "Playback & Monitor Output Selector (FB 0x06)",
            RouterNode{
                .inputs = std::move(selInputs),
                .outputs = std::move(selOutputs),
                .legalBundles = std::move(outSelBundles),
                .constraints = RouterConstraints{
                    .maxActiveBundles = 5,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 1, // Mixer Out L/R can feed AT MOST one destination
                },
            },
        },
        Node{nPhysOut, "Physical Outputs (8 Analog + 2 SPDIF)", EndpointNode{EndpointKind::Physical}},
    };

    // Ports
    // 1. Physical Inputs (8 Analog + 2 SPDIF)
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 1, "Line In " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{9}, nPhysIn, PortDirection::Output, 1, "SPDIF In L"});
    t.ports.push_back(Port{PortId{10}, nPhysIn, PortDirection::Output, 1, "SPDIF In R"});

    // 2. Host IO (10 Capture In, 10 Playback Out)
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostIO, PortDirection::Input, 1, "Host Capture " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{30 + i}, nHostIO, PortDirection::Output, 1, "Host Playback " + std::to_string(i)});
    }

    // 3. Mixer Stream Source Selector Ports
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nMixerStreamSrcSelector, PortDirection::Input, 1, "SrcMux In: Stream " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{51}, nMixerStreamSrcSelector, PortDirection::Output, 1, "SrcMux Out: Selected L"});
    t.ports.push_back(Port{PortId{52}, nMixerStreamSrcSelector, PortDirection::Output, 1, "SrcMux Out: Selected R"});

    // 4. 12x2 Mixer Ports
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{60 + i}, nMixer, PortDirection::Input, 1, "Mixer In: Analog " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{69}, nMixer, PortDirection::Input, 1, "Mixer In: SPDIF L"});
    t.ports.push_back(Port{PortId{70}, nMixer, PortDirection::Input, 1, "Mixer In: SPDIF R"});
    t.ports.push_back(Port{PortId{71}, nMixer, PortDirection::Input, 1, "Mixer In: Stream L"});
    t.ports.push_back(Port{PortId{72}, nMixer, PortDirection::Input, 1, "Mixer In: Stream R"});
    t.ports.push_back(Port{PortId{75}, nMixer, PortDirection::Output, 1, "Mixer Out L"});
    t.ports.push_back(Port{PortId{76}, nMixer, PortDirection::Output, 1, "Mixer Out R"});

    // 5. Output Selector (Router) Ports
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{80 + i}, nOutSelector, PortDirection::Input, 1, "OutMux In: Stream " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{91}, nOutSelector, PortDirection::Input, 1, "OutMux In: Mixer Out L"});
    t.ports.push_back(Port{PortId{92}, nOutSelector, PortDirection::Input, 1, "OutMux In: Mixer Out R"});
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{92 + i}, nOutSelector, PortDirection::Output, 1, "OutMux Out " + std::to_string(i)});
    }

    // 6. Physical Outputs
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{110 + i}, nPhysOut, PortDirection::Input, 1, "Line Out " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{119}, nPhysOut, PortDirection::Input, 1, "SPDIF Out L"});
    t.ports.push_back(Port{PortId{120}, nPhysOut, PortDirection::Input, 1, "SPDIF Out R"});

    // Fixed Links
    // 1. Direct capture: Physical In 1..10 -> Host Capture 1..10
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{20 + i}});
    }

    // 2. Physical In 1..10 -> Mixer Inputs 1..10
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{60 + i}});
    }

    // 3. Host Playback 1..10 -> Mixer Stream Source Selector Inputs
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{30 + i}, PortId{40 + i}});
    }

    // 4. Mixer Stream Source Selector Outputs -> Mixer Inputs 11/12
    t.fixedLinks.push_back(FixedLink{PortId{51}, PortId{71}});
    t.fixedLinks.push_back(FixedLink{PortId{52}, PortId{72}});

    // 5. Host Playback 1..10 -> Output Selector Inputs
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{30 + i}, PortId{80 + i}});
    }

    // 6. Mixer Outputs -> Output Selector Inputs
    t.fixedLinks.push_back(FixedLink{PortId{75}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{76}, PortId{92}});

    // 7. Output Selector Outputs -> Physical Outputs
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{92 + i}, PortId{110 + i}});
    }

    // Parameters
    t.parameters = {
        // Stream Playback Mute & Volume on Mixer
        Parameter{
            ParameterId{1},
            PortId{71},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Stream Playback Left Mute",
        },
        Parameter{
            ParameterId{2},
            PortId{72},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Stream Playback Right Mute",
        },
        Parameter{
            ParameterId{3},
            PortId{71},
            ParameterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Stream Playback Left Volume",
        },
        Parameter{
            ParameterId{4},
            PortId{72},
            ParameterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Stream Playback Right Volume",
        },
        // Mixer Output Mute & Volume
        Parameter{
            ParameterId{5},
            PortId{75},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Mixer Output Left Mute",
        },
        Parameter{
            ParameterId{6},
            PortId{76},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Mixer Output Right Mute",
        },
        Parameter{
            ParameterId{7},
            PortId{75},
            ParameterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Mixer Output Left Volume",
        },
        Parameter{
            ParameterId{8},
            PortId{76},
            ParameterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Mixer Output Right Volume",
        },
        // Clock Source Selection
        Parameter{
            ParameterId{9},
            nPhysIn,
            ParameterSemantic::ClockSource,
            EnumDomain{
                .values = {
                    EnumItem{0, "Internal (32k/44.1k/48k/88.2k/96k)"},
                    EnumItem{1, "S/PDIF Optical"},
                    EnumItem{2, "Word Clock BNC"},
                },
            },
            "Clock Source",
        },
    };

    // Meters (scalar channels)
    t.meters = {
        Meter{
            MeterId{1},
            PortId{75},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out L Peak Meter",
        },
        Meter{
            MeterId{2},
            PortId{76},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out R Peak Meter",
        },
    };

    return t;
}

void RunPhase88TopologyTests(TestContext& ctx) {
    auto phase88 = makeVirtualPhase88();
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

    // Verify Output Selector Router (5 direct playback bundles + 5 mixer destination bundles)
    auto* outMux = std::get_if<RouterNode>(&phase88.nodes[4].body);
    REQUIRE(ctx, outMux != nullptr);
    CHECK(ctx, outMux->legalBundles.size() == 10);
    CHECK(ctx, outMux->constraints.maxDestinationsPerInput == 1);

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
            MixerCrosspoint{CrosspointId{999}, PortId{61}, PortId{75}} // already exists
        );
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::DuplicateRouteOrCrosspoint);
        }
    }
}

} // namespace ASFW::LabTests
