#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Topology.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;

Topology makeVirtualSaffirePro24DSP() {
    Topology t;
    t.revision = 1;

    // Nodes
    const NodeId nPhysIn{1};
    const NodeId nHostIO{2};
    const NodeId nDiceRouter{3};
    const NodeId nMixer{4};
    const NodeId nOutGroup{5};
    const NodeId nChannelStrip{6};
    const NodeId nReverb{7};
    const NodeId nPhysOut{8};

    // 1. 46x46 DICE Router inputs and outputs
    std::vector<PortId> routerInputs;
    for (uint32_t i = 1; i <= 46; ++i) routerInputs.push_back(PortId{50 + i});

    std::vector<PortId> routerOutputs;
    for (uint32_t i = 1; i <= 46; ++i) routerOutputs.push_back(PortId{100 + i});

    // Complete 46x46 routing crossbar (2116 legal single-route bundles)
    std::vector<RouteBundle> routerBundles;
    routerBundles.reserve(46 * 46);
    uint32_t bundleId = 1;
    for (uint32_t inIdx = 1; inIdx <= 46; ++inIdx) {
        for (uint32_t outIdx = 1; outIdx <= 46; ++outIdx) {
            routerBundles.push_back(RouteBundle{
                RouteBundleId{bundleId++},
                {Route{PortId{50 + inIdx}, PortId{100 + outIdx}}},
            });
        }
    }

    // 2. 18x16 Hardware Mixer: 18 inputs -> 16 outputs = 288 crosspoints
    std::vector<PortId> mixerInputs;
    for (uint32_t i = 1; i <= 18; ++i) mixerInputs.push_back(PortId{150 + i});

    std::vector<PortId> mixerOutputs;
    for (uint32_t i = 1; i <= 16; ++i) mixerOutputs.push_back(PortId{170 + i});

    std::vector<MixerCrosspoint> mixerCrosspoints;
    mixerCrosspoints.reserve(18 * 16);
    uint32_t cpId = 1;
    for (uint32_t inIdx = 1; inIdx <= 18; ++inIdx) {
        for (uint32_t outIdx = 1; outIdx <= 16; ++outIdx) {
            mixerCrosspoints.push_back(MixerCrosspoint{
                CrosspointId{cpId++},
                PortId{150 + inIdx},
                PortId{170 + outIdx},
            });
        }
    }

    // 3. Processors
    std::vector<PortId> outGroupIn;
    for (uint32_t i = 1; i <= 6; ++i) outGroupIn.push_back(PortId{190 + i});
    std::vector<PortId> outGroupOut;
    for (uint32_t i = 1; i <= 10; ++i) outGroupOut.push_back(PortId{200 + i});

    t.nodes = {
        Node{nPhysIn, "Physical Inputs (6 Analog + 2 SPDIF + 8 ADAT)", EndpointNode{EndpointKind::Physical}},
        Node{nHostIO, "Host Audio Streams (16 In / 8 DAW Out)", EndpointNode{EndpointKind::Host}},
        Node{
            nDiceRouter,
            "DICE 46x46 Router Crossbar",
            RouterNode{
                .inputs = std::move(routerInputs),
                .outputs = std::move(routerOutputs),
                .legalBundles = std::move(routerBundles),
                .constraints = RouterConstraints{
                    .maxActiveRoutes = 128,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 46,
                },
            },
        },
        Node{
            nMixer,
            "18x16 Hardware Mixer",
            MixerNode{
                .inputs = std::move(mixerInputs),
                .outputs = std::move(mixerOutputs),
                .crosspoints = std::move(mixerCrosspoints),
            },
        },
        Node{
            nOutGroup,
            "Hardware Output Group (6 Line / 4 HP)",
            ProcessorNode{
                .inputs = std::move(outGroupIn),
                .outputs = std::move(outGroupOut),
            },
        },
        Node{
            nChannelStrip,
            "DSP Channel Strip (EQ & Compressor)",
            ProcessorNode{
                .inputs = {PortId{211}, PortId{212}},
                .outputs = {PortId{213}, PortId{214}},
            },
        },
        Node{
            nReverb,
            "DSP Reverb Engine",
            ProcessorNode{
                .inputs = {PortId{221}, PortId{222}},
                .outputs = {PortId{223}, PortId{224}},
            },
        },
        Node{nPhysOut, "Physical Outputs (6 Analog, 4 Headphone, 2 SPDIF)", EndpointNode{EndpointKind::Physical}},
    };

    // Ports
    // 1. Physical Inputs (6 Analog + 2 SPDIF + 8 ADAT = 16 ch)
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 1, "Phys In: Analog " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{7}, nPhysIn, PortDirection::Output, 1, "Phys In: SPDIF L"});
    t.ports.push_back(Port{PortId{8}, nPhysIn, PortDirection::Output, 1, "Phys In: SPDIF R"});
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{8 + i}, nPhysIn, PortDirection::Output, 1, "Phys In: ADAT " + std::to_string(i)});
    }

    // 2. Host IO (16 Capture In, 8 Playback Out)
    for (uint32_t i = 1; i <= 16; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostIO, PortDirection::Input, 1, "Host Stream Capture " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nHostIO, PortDirection::Output, 1, "DAW Stream Playback " + std::to_string(i)});
    }

    // 3. Router Inputs (46 total: 16 Phys + 8 DAW + 16 MixerOut + 2 ChStripOut + 2 ReverbOut + 2 Optical SPDIF)
    for (uint32_t i = 1; i <= 46; ++i) {
        t.ports.push_back(Port{PortId{50 + i}, nDiceRouter, PortDirection::Input, 1, "Router In " + std::to_string(i)});
    }

    // 4. Router Outputs (46 total: 6 AnalogOut + 2 SpdifOut + 16 StreamCap + 18 MixerIn + 2 ChStripIn + 2 ReverbIn)
    for (uint32_t i = 1; i <= 46; ++i) {
        t.ports.push_back(Port{PortId{100 + i}, nDiceRouter, PortDirection::Output, 1, "Router Out " + std::to_string(i)});
    }

    // 5. Mixer Ports (18 in, 16 out)
    for (uint32_t i = 1; i <= 18; ++i) {
        t.ports.push_back(Port{PortId{150 + i}, nMixer, PortDirection::Input, 1, "Mixer In " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 16; ++i) {
        t.ports.push_back(Port{PortId{170 + i}, nMixer, PortDirection::Output, 1, "Mixer Out " + std::to_string(i)});
    }

    // 6. Output Group Ports (6 in -> 10 out)
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{190 + i}, nOutGroup, PortDirection::Input, 1, "OutGroup In " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{200 + i}, nOutGroup, PortDirection::Output, 1, "OutGroup Phone Out " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{206 + i}, nOutGroup, PortDirection::Output, 1, "OutGroup HP Out " + std::to_string(i)});
    }

    // 7. Channel Strip & Reverb Ports
    t.ports.push_back(Port{PortId{211}, nChannelStrip, PortDirection::Input, 1, "ChStrip In L"});
    t.ports.push_back(Port{PortId{212}, nChannelStrip, PortDirection::Input, 1, "ChStrip In R"});
    t.ports.push_back(Port{PortId{213}, nChannelStrip, PortDirection::Output, 1, "ChStrip Out L"});
    t.ports.push_back(Port{PortId{214}, nChannelStrip, PortDirection::Output, 1, "ChStrip Out R"});

    t.ports.push_back(Port{PortId{221}, nReverb, PortDirection::Input, 1, "Reverb In L"});
    t.ports.push_back(Port{PortId{222}, nReverb, PortDirection::Input, 1, "Reverb In R"});
    t.ports.push_back(Port{PortId{223}, nReverb, PortDirection::Output, 1, "Reverb Out L"});
    t.ports.push_back(Port{PortId{224}, nReverb, PortDirection::Output, 1, "Reverb Out R"});

    // 8. Physical Outputs (6 Phone + 4 HP + 2 SPDIF = 12 ch)
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{230 + i}, nPhysOut, PortDirection::Input, 1, "Phone Out " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{236 + i}, nPhysOut, PortDirection::Input, 1, "HP Out " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{241}, nPhysOut, PortDirection::Input, 1, "SPDIF Out L"});
    t.ports.push_back(Port{PortId{242}, nPhysOut, PortDirection::Input, 1, "SPDIF Out R"});

    // Fixed Links
    // 1. Phys In 1..16 -> Router In 1..16
    for (uint32_t i = 1; i <= 16; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{50 + i}});
    }
    // 2. DAW Playback 1..8 -> Router In 17..24
    for (uint32_t i = 1; i <= 8; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{40 + i}, PortId{66 + i}});
    }
    // 3. Mixer Out 1..16 -> Router In 25..40
    for (uint32_t i = 1; i <= 16; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{170 + i}, PortId{74 + i}});
    }
    // 4. ChStrip Out -> Router In 41..42
    t.fixedLinks.push_back(FixedLink{PortId{213}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{214}, PortId{92}});
    // 5. Reverb Out -> Router In 43..44
    t.fixedLinks.push_back(FixedLink{PortId{223}, PortId{93}});
    t.fixedLinks.push_back(FixedLink{PortId{224}, PortId{94}});

    // 6. Router Out 1..6 (Analog Out) -> Output Group In 1..6
    for (uint32_t i = 1; i <= 6; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{100 + i}, PortId{190 + i}});
    }
    // 7. Router Out 7..8 (SPDIF Out) -> Physical SPDIF Out
    t.fixedLinks.push_back(FixedLink{PortId{107}, PortId{241}});
    t.fixedLinks.push_back(FixedLink{PortId{108}, PortId{242}});

    // 8. Router Out 9..24 (Stream Cap) -> Host Capture 1..16
    for (uint32_t i = 1; i <= 16; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{108 + i}, PortId{20 + i}});
    }
    // 9. Router Out 25..42 (Mixer In) -> Mixer Inputs 1..18
    for (uint32_t i = 1; i <= 18; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{124 + i}, PortId{150 + i}});
    }
    // 10. Router Out 43..44 -> ChStrip In
    t.fixedLinks.push_back(FixedLink{PortId{143}, PortId{211}});
    t.fixedLinks.push_back(FixedLink{PortId{144}, PortId{212}});
    // 11. Router Out 45..46 -> Reverb In
    t.fixedLinks.push_back(FixedLink{PortId{145}, PortId{221}});
    t.fixedLinks.push_back(FixedLink{PortId{146}, PortId{222}});

    // 12. Output Group Out -> Physical Outs
    for (uint32_t i = 1; i <= 6; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{200 + i}, PortId{230 + i}});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{206 + i}, PortId{236 + i}});
    }

    // Parameters
    t.parameters = {
        // Output Group Parameters (0x50 block)
        Parameter{
            ParameterId{1},
            nOutGroup,
            ParameterSemantic::Dim,
            BooleanDomain{},
            "Master Dim Enabled",
        },
        Parameter{
            ParameterId{2},
            nOutGroup,
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Master Mute Enabled",
        },
        // Per-Output Volumes (0..127) for Output Group
        Parameter{
            ParameterId{3},
            PortId{201},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 127.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Monitor 1 Volume",
        },
        Parameter{
            ParameterId{4},
            PortId{202},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 127.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Monitor 2 Volume",
        },
        // Mic Input Level (Ch 1/2): Line vs Instrument
        Parameter{
            ParameterId{5},
            PortId{1},
            ParameterSemantic::NominalLevel,
            EnumDomain{
                .values = {
                    EnumItem{0, "Line (-10dB to +36dB)"},
                    EnumItem{1, "Instrument (+13dB to +60dB)"},
                },
            },
            "Ch 1 Input Level",
        },
        Parameter{
            ParameterId{6},
            PortId{2},
            ParameterSemantic::NominalLevel,
            EnumDomain{
                .values = {
                    EnumItem{0, "Line (-10dB to +36dB)"},
                    EnumItem{1, "Instrument (+13dB to +60dB)"},
                },
            },
            "Ch 2 Input Level",
        },
        // Optical Output Mode
        Parameter{
            ParameterId{7},
            nPhysOut,
            ParameterSemantic::Unknown,
            EnumDomain{
                .values = {
                    EnumItem{0, "ADAT"},
                    EnumItem{1, "S/PDIF"},
                },
            },
            "Optical Out Interface Mode",
        },
    };

    // Meters (scalar channels)
    t.meters = {
        Meter{
            MeterId{1},
            PortId{1},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mic/Inst 1 Peak Meter",
        },
        Meter{
            MeterId{2},
            PortId{2},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mic/Inst 2 Peak Meter",
        },
        Meter{
            MeterId{3},
            PortId{201},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Monitor 1 Peak Meter",
        },
    };

    return t;
}

void RunSaffireTopologyTests(TestContext& ctx) {
    auto saffire = makeVirtualSaffirePro24DSP();
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
