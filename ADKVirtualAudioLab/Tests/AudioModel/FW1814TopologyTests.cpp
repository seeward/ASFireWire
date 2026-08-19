#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Topology.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;

Topology makeVirtualFW1814() {
    Topology t;
    t.revision = 1;

    // Nodes
    const NodeId nPhysIn{1};
    const NodeId nHostCaptureBus{2};
    const NodeId nHostIO{3};
    const NodeId nSumMixer{4};
    const NodeId nAuxMixer{5};
    const NodeId nHpMux{6};
    const NodeId nLineOutMux{7};
    const NodeId nPhysOut{8};

    std::vector<PortId> capInPorts;
    std::vector<PortId> capOutPorts;
    for (uint32_t i = 1; i <= 9; ++i) {
        capInPorts.push_back(PortId{10 + i});
        capOutPorts.push_back(PortId{20 + i});
    }

    // 1. Main Sum Mixer: 22x4 / 11 stereo pairs -> 2 stereo output pairs (Mix 0, Mix 1)
    std::vector<PortId> sumMixerInputs;
    for (uint32_t i = 1; i <= 11; ++i) sumMixerInputs.push_back(PortId{50 + i});
    std::vector<PortId> sumMixerOutputs = {PortId{71}, PortId{72}};

    std::vector<MixerCrosspoint> sumCrosspoints;
    uint32_t cpId = 1;
    // 11 stereo inputs x 2 stereo destinations = 22 stereo crosspoints
    for (uint32_t src = 1; src <= 11; ++src) {
        sumCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{50 + src}, PortId{71}}); // In -> Mix 0
        sumCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{50 + src}, PortId{72}}); // In -> Mix 1
    }

    // 2. Aux Downmix Mixer: 22x2 / 11 stereo pairs -> 1 stereo output pair (Aux 0)
    std::vector<PortId> auxInputs;
    for (uint32_t i = 1; i <= 11; ++i) auxInputs.push_back(PortId{100 + i});
    std::vector<PortId> auxOutputs = {PortId{112}};

    std::vector<MixerCrosspoint> auxCrosspoints;
    for (uint32_t src = 1; src <= 11; ++src) {
        auxCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{100 + src}, PortId{112}}); // In -> Aux 0
    }

    t.nodes = {
        Node{nPhysIn, "Physical Inputs (8 Analog + 2 SPDIF + 8 ADAT)", EndpointNode{EndpointKind::Physical}},
        Node{
            nHostCaptureBus,
            "I18S Host Capture Bus",
            ProcessorNode{
                .inputs = std::move(capInPorts),
                .outputs = std::move(capOutPorts),
            },
        },
        Node{nHostIO, "Host Audio Streams (18 In / 14 Out)", EndpointNode{EndpointKind::Host}},
        Node{
            nSumMixer,
            "Main 22x4 Sum Matrix (11 Stereo Pairs -> 2 Stereo Destination Pairs)",
            MixerNode{
                .inputs = std::move(sumMixerInputs),
                .outputs = std::move(sumMixerOutputs),
                .crosspoints = std::move(sumCrosspoints),
            },
        },
        Node{
            nAuxMixer,
            "Aux 22x2 Downmix Matrix (11 Stereo Pairs -> 1 Stereo Destination Pair)",
            MixerNode{
                .inputs = std::move(auxInputs),
                .outputs = std::move(auxOutputs),
                .crosspoints = std::move(auxCrosspoints),
            },
        },
        Node{
            nHpMux,
            "Headphone 1 & 2 Source Selectors",
            RouterNode{
                .inputs = {PortId{81}, PortId{82}, PortId{83}}, // Mix 0, Mix 1, Aux 0
                .outputs = {PortId{84}, PortId{85}},            // HP 1, HP 2
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{81}, PortId{84}}}}, // Mix 0 -> HP 1
                    RouteBundle{RouteBundleId{2}, {Route{PortId{82}, PortId{84}}}}, // Mix 1 -> HP 1
                    RouteBundle{RouteBundleId{3}, {Route{PortId{83}, PortId{84}}}}, // Aux 0 -> HP 1
                    RouteBundle{RouteBundleId{4}, {Route{PortId{81}, PortId{85}}}}, // Mix 0 -> HP 2
                    RouteBundle{RouteBundleId{5}, {Route{PortId{82}, PortId{85}}}}, // Mix 1 -> HP 2
                    RouteBundle{RouteBundleId{6}, {Route{PortId{83}, PortId{85}}}}, // Aux 0 -> HP 2
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 2,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 2,
                },
            },
        },
        Node{
            nLineOutMux,
            "Analog Line Out Source Selectors (analog_pair_sources)",
            RouterNode{
                .inputs = {PortId{121}, PortId{122}, PortId{123}}, // Mix 0, Mix 1, Aux 0
                .outputs = {PortId{124}, PortId{125}},              // LineOut 1/2, LineOut 3/4
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{121}, PortId{124}}}}, // Mix 0 -> LineOut 1/2
                    RouteBundle{RouteBundleId{2}, {Route{PortId{123}, PortId{124}}}}, // Aux 0 -> LineOut 1/2
                    RouteBundle{RouteBundleId{3}, {Route{PortId{122}, PortId{125}}}}, // Mix 1 -> LineOut 3/4
                    RouteBundle{RouteBundleId{4}, {Route{PortId{123}, PortId{125}}}}, // Aux 0 -> LineOut 3/4
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 2,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 2,
                },
            },
        },
        Node{nPhysOut, "Physical Outputs (4 Analog + 2 SPDIF + 8 ADAT + 2 HP)", EndpointNode{EndpointKind::Physical}},
    };

    // Ports (all stereo pairs = 2 channels)
    // 1. Physical Inputs (9 stereo pairs = 18 channels)
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 2, "Line In " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{5}, nPhysIn, PortDirection::Output, 2, "SPDIF In"});
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{5 + i}, nPhysIn, PortDirection::Output, 2, "ADAT In " + std::to_string(i)});
    }

    // 2. I18S Host Capture Bus ports
    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{10 + i}, nHostCaptureBus, PortDirection::Input, 2, "I18S Bus In Pair " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostCaptureBus, PortDirection::Output, 2, "I18S Bus Out Pair " + std::to_string(i)});
    }

    // 3. Host IO streams (9 capture pairs = 18 ch, 7 playback pairs = 14 ch)
    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{30 + i}, nHostIO, PortDirection::Input, 2, "Host Capture Pair " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 7; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nHostIO, PortDirection::Output, 2, "Host Playback Stream " + std::to_string(i * 2 - 1) + "/" + std::to_string(i * 2)});
    }

    // 4. Main 22x4 Sum Matrix ports (11 stereo inputs, 2 stereo outputs)
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{50 + i}, nSumMixer, PortDirection::Input, 2, "Matrix In: LineIn " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{55}, nSumMixer, PortDirection::Input, 2, "Matrix In: SpdifIn"});
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{55 + i}, nSumMixer, PortDirection::Input, 2, "Matrix In: AdatIn " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{60}, nSumMixer, PortDirection::Input, 2, "Matrix In: Playback 1/2"});
    t.ports.push_back(Port{PortId{61}, nSumMixer, PortDirection::Input, 2, "Matrix In: Playback 3/4"});
    t.ports.push_back(Port{PortId{71}, nSumMixer, PortDirection::Output, 2, "Matrix Out: Mix 0 (LineOut 1/2)"});
    t.ports.push_back(Port{PortId{72}, nSumMixer, PortDirection::Output, 2, "Matrix Out: Mix 1 (LineOut 3/4)"});

    // 5. Aux 22x2 Downmix ports (11 stereo inputs, 1 stereo output)
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{100 + i}, nAuxMixer, PortDirection::Input, 2, "Aux In: LineIn " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{105}, nAuxMixer, PortDirection::Input, 2, "Aux In: SpdifIn"});
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{105 + i}, nAuxMixer, PortDirection::Input, 2, "Aux In: AdatIn " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{110}, nAuxMixer, PortDirection::Input, 2, "Aux In: Playback 1/2"});
    t.ports.push_back(Port{PortId{111}, nAuxMixer, PortDirection::Input, 2, "Aux In: Playback 3/4"});
    t.ports.push_back(Port{PortId{112}, nAuxMixer, PortDirection::Output, 2, "Aux Out: Aux 0"});

    // 6. Headphone Mux ports
    t.ports.push_back(Port{PortId{81}, nHpMux, PortDirection::Input, 2, "HpMux In: Mix 0"});
    t.ports.push_back(Port{PortId{82}, nHpMux, PortDirection::Input, 2, "HpMux In: Mix 1"});
    t.ports.push_back(Port{PortId{83}, nHpMux, PortDirection::Input, 2, "HpMux In: Aux 0"});
    t.ports.push_back(Port{PortId{84}, nHpMux, PortDirection::Output, 2, "HpMux Out: HP 1"});
    t.ports.push_back(Port{PortId{85}, nHpMux, PortDirection::Output, 2, "HpMux Out: HP 2"});

    // 7. Analog Line Out Mux ports
    t.ports.push_back(Port{PortId{121}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Mix 0"});
    t.ports.push_back(Port{PortId{122}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Mix 1"});
    t.ports.push_back(Port{PortId{123}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Aux 0"});
    t.ports.push_back(Port{PortId{124}, nLineOutMux, PortDirection::Output, 2, "LineMux Out: LineOut 1/2"});
    t.ports.push_back(Port{PortId{125}, nLineOutMux, PortDirection::Output, 2, "LineMux Out: LineOut 3/4"});

    // 8. Physical Outputs
    t.ports.push_back(Port{PortId{91}, nPhysOut, PortDirection::Input, 2, "Phys Line Out 1/2"});
    t.ports.push_back(Port{PortId{92}, nPhysOut, PortDirection::Input, 2, "Phys Line Out 3/4"});
    t.ports.push_back(Port{PortId{93}, nPhysOut, PortDirection::Input, 2, "Phys S/PDIF Out"});
    t.ports.push_back(Port{PortId{94}, nPhysOut, PortDirection::Input, 2, "Phys ADAT Out 1/2"});
    t.ports.push_back(Port{PortId{95}, nPhysOut, PortDirection::Input, 2, "Phys ADAT Out 3/4"});
    t.ports.push_back(Port{PortId{96}, nPhysOut, PortDirection::Input, 2, "Phys ADAT Out 5/6"});
    t.ports.push_back(Port{PortId{97}, nPhysOut, PortDirection::Input, 2, "Phys ADAT Out 7/8"});
    t.ports.push_back(Port{PortId{98}, nPhysOut, PortDirection::Input, 2, "Phys Headphone 1"});
    t.ports.push_back(Port{PortId{99}, nPhysOut, PortDirection::Input, 2, "Phys Headphone 2"});

    // Fixed Links
    // 1. Physical In -> I18S Bus
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{10 + i}});
    }
    // 2. I18S Bus -> Host Capture
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{20 + i}, PortId{30 + i}});
    }
    // 3. Physical In -> Main Sum Matrix & Aux Matrix
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{50 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{100 + i}});
    }
    // 4. Host Playback 1/2 & 3/4 -> Main Sum Matrix & Aux Matrix
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{60}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{61}});
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{110}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{111}});

    // 5. Later Host Playback streams -> Physical Digital Outputs (SPDIF & ADAT)
    t.fixedLinks.push_back(FixedLink{PortId{43}, PortId{93}}); // Stream 5/6 -> SPDIF Out
    t.fixedLinks.push_back(FixedLink{PortId{44}, PortId{94}}); // Stream 7/8 -> ADAT Out 1/2
    t.fixedLinks.push_back(FixedLink{PortId{45}, PortId{95}}); // Stream 9/10 -> ADAT Out 3/4
    t.fixedLinks.push_back(FixedLink{PortId{46}, PortId{96}}); // Stream 11/12 -> ADAT Out 5/6
    t.fixedLinks.push_back(FixedLink{PortId{47}, PortId{97}}); // Stream 13/14 -> ADAT Out 7/8

    // 6. Sum Matrix Out (Mix 0 & 1) -> LineOutMux & HpMux
    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{121}}); // Mix 0 -> LineOutMux
    t.fixedLinks.push_back(FixedLink{PortId{72}, PortId{122}}); // Mix 1 -> LineOutMux
    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{81}});  // Mix 0 -> HpMux
    t.fixedLinks.push_back(FixedLink{PortId{72}, PortId{82}});  // Mix 1 -> HpMux

    // 7. Aux Matrix Out (Aux 0) -> LineOutMux & HpMux
    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{123}}); // Aux 0 -> LineOutMux
    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{83}});  // Aux 0 -> HpMux

    // 8. LineOutMux -> Physical Line Outs
    t.fixedLinks.push_back(FixedLink{PortId{124}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{125}, PortId{92}});

    // 9. Headphone Mux Out -> Physical Headphone Outs
    t.fixedLinks.push_back(FixedLink{PortId{84}, PortId{98}});
    t.fixedLinks.push_back(FixedLink{PortId{85}, PortId{99}});

    // Parameters (Parameter Window +0x00..0x80)
    t.parameters = {
        // Stream Input Gains (Playback 1/2, 3/4)
        Parameter{
            ParameterId{1},
            PortId{60},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Stream Playback 1/2 Input Gain",
        },
        // Analog Output Volumes (LineOut 1/2, 3/4)
        Parameter{
            ParameterId{2},
            PortId{91},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Analog Output Volume 1/2",
        },
        Parameter{
            ParameterId{3},
            PortId{92},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Analog Output Volume 3/4",
        },
        // Headphone Volumes (+0x38..0x40)
        Parameter{
            ParameterId{4},
            PortId{98},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Headphone 1 Volume",
        },
        Parameter{
            ParameterId{5},
            PortId{99},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Headphone 2 Volume",
        },
    };

    // Meters (scalar channels)
    t.meters = {
        Meter{
            MeterId{1},
            PortId{1},
            MeterSemantic::Peak,
            ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "LineIn 1/2 Peak Meter",
        },
        Meter{
            MeterId{2},
            PortId{71},
            MeterSemantic::Peak,
            ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer 0 Peak Meter",
        },
        Meter{
            MeterId{3},
            PortId{84},
            MeterSemantic::Peak,
            ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Headphone 1 Peak Meter",
        },
    };

    return t;
}

void RunFW1814TopologyTests(TestContext& ctx) {
    auto fw1814 = makeVirtualFW1814();
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
        invalid.fixedLinks.push_back(FixedLink{PortId{2}, PortId{11}}); // Port 11 is already driven by Port 1
        auto res = validate(invalid);
        CHECK(ctx, !res.has_value());
        if (!res.has_value()) {
            CHECK(ctx, res.error().kind == TopologyErrorKind::MultipleDriversOnInput);
        }
    }
}

} // namespace ASFW::LabTests
