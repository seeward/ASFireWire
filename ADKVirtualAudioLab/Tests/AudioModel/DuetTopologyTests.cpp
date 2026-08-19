#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Topology.hpp"
#include "../../Core/AudioModel/Validate.hpp"

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;

Topology makeVirtualDuet() {
    Topology t;
    t.revision = 1;

    // Nodes
    const NodeId nPhysIn{1};
    const NodeId nInMux{2};
    const NodeId nPreamp{3};
    const NodeId nHostIO{4};
    const NodeId nMixer{5};
    const NodeId nOutMux{6};
    const NodeId nOutStage{7};
    const NodeId nPhysOut{8};

    t.nodes = {
        Node{nPhysIn, "Physical Inputs", EndpointNode{EndpointKind::Physical}},
        Node{
            nInMux,
            "Input Source Router",
            RouterNode{
                .inputs = {PortId{11}, PortId{12}, PortId{13}, PortId{14}},
                .outputs = {PortId{15}, PortId{16}},
                // 4 independent 1-route bundles (hardware controls Ch 1 and Ch 2 source independently)
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{11}, PortId{15}}}}, // XLR 1 -> Selected 1
                    RouteBundle{RouteBundleId{2}, {Route{PortId{12}, PortId{15}}}}, // Inst 1 -> Selected 1
                    RouteBundle{RouteBundleId{3}, {Route{PortId{13}, PortId{16}}}}, // XLR 2 -> Selected 2
                    RouteBundle{RouteBundleId{4}, {Route{PortId{14}, PortId{16}}}}, // Inst 2 -> Selected 2
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 2,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 1,
                },
            },
        },
        Node{
            nPreamp,
            "Analog Preamp Stage",
            ProcessorNode{
                .inputs = {PortId{21}, PortId{22}},
                .outputs = {PortId{23}, PortId{24}},
            },
        },
        Node{nHostIO, "Host Audio Streams", EndpointNode{EndpointKind::Host}},
        Node{
            nMixer,
            "Hardware Low-Latency Mixer",
            MixerNode{
                .inputs = {PortId{41}, PortId{42}, PortId{43}, PortId{44}},
                .outputs = {PortId{45}, PortId{46}},
                .crosspoints = {
                    // Full 4x2 matrix matching Apogee Duet MixerCoefficients
                    MixerCrosspoint{CrosspointId{1}, PortId{41}, PortId{45}}, // Analog 1 -> Out L
                    MixerCrosspoint{CrosspointId{2}, PortId{41}, PortId{46}}, // Analog 1 -> Out R (pan)
                    MixerCrosspoint{CrosspointId{3}, PortId{42}, PortId{45}}, // Analog 2 -> Out L (pan)
                    MixerCrosspoint{CrosspointId{4}, PortId{42}, PortId{46}}, // Analog 2 -> Out R
                    MixerCrosspoint{CrosspointId{5}, PortId{43}, PortId{45}}, // Playback 1 -> Out L
                    MixerCrosspoint{CrosspointId{6}, PortId{43}, PortId{46}}, // Playback 1 -> Out R (pan)
                    MixerCrosspoint{CrosspointId{7}, PortId{44}, PortId{45}}, // Playback 2 -> Out L (pan)
                    MixerCrosspoint{CrosspointId{8}, PortId{44}, PortId{46}}, // Playback 2 -> Out R
                },
            },
        },
        Node{
            nOutMux,
            "Output Source Router",
            RouterNode{
                .inputs = {PortId{51}, PortId{52}, PortId{53}, PortId{54}},
                .outputs = {PortId{55}, PortId{56}},
                // 2 coupled stereo bundles (output source switches Playback 1/2 vs Mixer Out 1/2 in lockstep)
                .legalBundles = {
                    RouteBundle{
                        RouteBundleId{1},
                        {
                            Route{PortId{51}, PortId{55}}, // Playback 1 -> Master L
                            Route{PortId{52}, PortId{56}}, // Playback 2 -> Master R
                        },
                    },
                    RouteBundle{
                        RouteBundleId{2},
                        {
                            Route{PortId{53}, PortId{55}}, // Mixer Out L -> Master L
                            Route{PortId{54}, PortId{56}}, // Mixer Out R -> Master R
                        },
                    },
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 1,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 1,
                },
            },
        },
        Node{
            nOutStage,
            "Master Output Stage",
            ProcessorNode{
                .inputs = {PortId{57}, PortId{58}},
                .outputs = {PortId{59}, PortId{60}},
            },
        },
        Node{nPhysOut, "Physical Outputs", EndpointNode{EndpointKind::Physical}},
    };

    // Ports
    t.ports = {
        // Physical In jacks (outputs into device)
        Port{PortId{1}, nPhysIn, PortDirection::Output, 1, "XLR In 1"},
        Port{PortId{2}, nPhysIn, PortDirection::Output, 1, "XLR In 2"},
        Port{PortId{3}, nPhysIn, PortDirection::Output, 1, "Inst In 1"},
        Port{PortId{4}, nPhysIn, PortDirection::Output, 1, "Inst In 2"},

        // Input Router ports
        Port{PortId{11}, nInMux, PortDirection::Input, 1, "Mux In: XLR 1"},
        Port{PortId{12}, nInMux, PortDirection::Input, 1, "Mux In: Inst 1"},
        Port{PortId{13}, nInMux, PortDirection::Input, 1, "Mux In: XLR 2"},
        Port{PortId{14}, nInMux, PortDirection::Input, 1, "Mux In: Inst 2"},
        Port{PortId{15}, nInMux, PortDirection::Output, 1, "Mux Out: Selected 1"},
        Port{PortId{16}, nInMux, PortDirection::Output, 1, "Mux Out: Selected 2"},

        // Preamp Stage ports
        Port{PortId{21}, nPreamp, PortDirection::Input, 1, "Preamp In 1"},
        Port{PortId{22}, nPreamp, PortDirection::Input, 1, "Preamp In 2"},
        Port{PortId{23}, nPreamp, PortDirection::Output, 1, "Preamp Out 1"},
        Port{PortId{24}, nPreamp, PortDirection::Output, 1, "Preamp Out 2"},

        // Host IO streams
        Port{PortId{31}, nHostIO, PortDirection::Input, 1, "Host Stream Capture 1"},
        Port{PortId{32}, nHostIO, PortDirection::Input, 1, "Host Stream Capture 2"},
        Port{PortId{33}, nHostIO, PortDirection::Output, 1, "Host Stream Playback 1"},
        Port{PortId{34}, nHostIO, PortDirection::Output, 1, "Host Stream Playback 2"},

        // Mixer ports
        Port{PortId{41}, nMixer, PortDirection::Input, 1, "Mixer In: Analog 1"},
        Port{PortId{42}, nMixer, PortDirection::Input, 1, "Mixer In: Analog 2"},
        Port{PortId{43}, nMixer, PortDirection::Input, 1, "Mixer In: Playback 1"},
        Port{PortId{44}, nMixer, PortDirection::Input, 1, "Mixer In: Playback 2"},
        Port{PortId{45}, nMixer, PortDirection::Output, 1, "Mixer Out L"},
        Port{PortId{46}, nMixer, PortDirection::Output, 1, "Mixer Out R"},

        // Output Router ports
        Port{PortId{51}, nOutMux, PortDirection::Input, 1, "OutMux In: Playback 1"},
        Port{PortId{52}, nOutMux, PortDirection::Input, 1, "OutMux In: Playback 2"},
        Port{PortId{53}, nOutMux, PortDirection::Input, 1, "OutMux In: Mixer Out L"},
        Port{PortId{54}, nOutMux, PortDirection::Input, 1, "OutMux In: Mixer Out R"},
        Port{PortId{55}, nOutMux, PortDirection::Output, 1, "OutMux Out: Selected L"},
        Port{PortId{56}, nOutMux, PortDirection::Output, 1, "OutMux Out: Selected R"},

        // Output Stage ports
        Port{PortId{57}, nOutStage, PortDirection::Input, 1, "OutStage In L"},
        Port{PortId{58}, nOutStage, PortDirection::Input, 1, "OutStage In R"},
        Port{PortId{59}, nOutStage, PortDirection::Output, 1, "OutStage Out L"},
        Port{PortId{60}, nOutStage, PortDirection::Output, 1, "OutStage Out R"},

        // Physical Outputs (inputs from internal stage)
        Port{PortId{61}, nPhysOut, PortDirection::Input, 1, "Line Out L"},
        Port{PortId{62}, nPhysOut, PortDirection::Input, 1, "Line Out R"},
        Port{PortId{63}, nPhysOut, PortDirection::Input, 1, "Headphone Out L"},
        Port{PortId{64}, nPhysOut, PortDirection::Input, 1, "Headphone Out R"},
    };

    // Fixed Links
    t.fixedLinks = {
        // Physical jacks to Input Mux
        FixedLink{PortId{1}, PortId{11}},
        FixedLink{PortId{3}, PortId{12}},
        FixedLink{PortId{2}, PortId{13}},
        FixedLink{PortId{4}, PortId{14}},

        // Input Mux to Preamp
        FixedLink{PortId{15}, PortId{21}},
        FixedLink{PortId{16}, PortId{22}},

        // Preamp to Host Capture
        FixedLink{PortId{23}, PortId{31}},
        FixedLink{PortId{24}, PortId{32}},

        // Preamp to Mixer Analog Inputs
        FixedLink{PortId{23}, PortId{41}},
        FixedLink{PortId{24}, PortId{42}},

        // Host Playback to Mixer Stream Inputs
        FixedLink{PortId{33}, PortId{43}},
        FixedLink{PortId{34}, PortId{44}},

        // Host Playback to Output Mux
        FixedLink{PortId{33}, PortId{51}},
        FixedLink{PortId{34}, PortId{52}},

        // Mixer Out to Output Mux
        FixedLink{PortId{45}, PortId{53}},
        FixedLink{PortId{46}, PortId{54}},

        // Output Mux to Master Output Stage
        FixedLink{PortId{55}, PortId{57}},
        FixedLink{PortId{56}, PortId{58}},

        // Master Output Stage to Line Out
        FixedLink{PortId{59}, PortId{61}},
        FixedLink{PortId{60}, PortId{62}},

        // Master Output Stage to Headphone Out
        FixedLink{PortId{59}, PortId{63}},
        FixedLink{PortId{60}, PortId{64}},
    };

    // Parameters
    t.parameters = {
        // Preamp Gains targeting specific preamp channel input ports (XLR mic range: 10..75 dB)
        Parameter{
            ParameterId{1},
            PortId{21},
            ParameterSemantic::Level,
            ScalarDomain{.min = 10.0, .max = 75.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Input 1 Preamp Gain",
        },
        Parameter{
            ParameterId{2},
            PortId{22},
            ParameterSemantic::Level,
            ScalarDomain{.min = 10.0, .max = 75.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Input 2 Preamp Gain",
        },
        // Phantom Power
        Parameter{
            ParameterId{3},
            PortId{1},
            ParameterSemantic::PhantomPower,
            BooleanDomain{},
            "Input 1 +48V",
        },
        Parameter{
            ParameterId{4},
            PortId{2},
            ParameterSemantic::PhantomPower,
            BooleanDomain{},
            "Input 2 +48V",
        },
        // Phase Invert targeting specific channels
        Parameter{
            ParameterId{5},
            PortId{21},
            ParameterSemantic::PhaseInvert,
            BooleanDomain{},
            "Input 1 Phase Invert",
        },
        Parameter{
            ParameterId{6},
            PortId{22},
            ParameterSemantic::PhaseInvert,
            BooleanDomain{},
            "Input 2 Phase Invert",
        },
        // XLR Nominal Level
        Parameter{
            ParameterId{7},
            PortId{1},
            ParameterSemantic::NominalLevel,
            EnumDomain{
                .values = {
                    EnumItem{0, "Microphone (Variable Gain)"},
                    EnumItem{1, "Professional (+4 dBu)"},
                    EnumItem{2, "Consumer (-10 dBV)"},
                },
            },
            "Input 1 XLR Nominal Level",
        },
        Parameter{
            ParameterId{8},
            PortId{2},
            ParameterSemantic::NominalLevel,
            EnumDomain{
                .values = {
                    EnumItem{0, "Microphone (Variable Gain)"},
                    EnumItem{1, "Professional (+4 dBu)"},
                    EnumItem{2, "Consumer (-10 dBV)"},
                },
            },
            "Input 2 XLR Nominal Level",
        },
        // Output Stage Volume (-64..0 dB) and Mute
        Parameter{
            ParameterId{9},
            nOutStage,
            ParameterSemantic::Level,
            ScalarDomain{.min = -64.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Master Output Volume",
        },
        Parameter{
            ParameterId{10},
            nOutStage,
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Master Output Mute",
        },
        // All 8 Mixer Crosspoint Gains (native 0..0x3FFF coefficient range)
        Parameter{
            ParameterId{11},
            CrosspointId{1},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Analog 1 -> Out L Gain",
        },
        Parameter{
            ParameterId{12},
            CrosspointId{2},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Analog 1 -> Out R Gain",
        },
        Parameter{
            ParameterId{13},
            CrosspointId{3},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Analog 2 -> Out L Gain",
        },
        Parameter{
            ParameterId{14},
            CrosspointId{4},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Analog 2 -> Out R Gain",
        },
        Parameter{
            ParameterId{15},
            CrosspointId{5},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Playback 1 -> Out L Gain",
        },
        Parameter{
            ParameterId{16},
            CrosspointId{6},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Playback 1 -> Out R Gain",
        },
        Parameter{
            ParameterId{17},
            CrosspointId{7},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Playback 2 -> Out L Gain",
        },
        Parameter{
            ParameterId{18},
            CrosspointId{8},
            ParameterSemantic::Level,
            ScalarDomain{.min = 0.0, .max = 16383.0, .step = 1.0, .unit = ScalarUnit::Generic},
            "Mixer Playback 2 -> Out R Gain",
        },
    };

    // Meters (scalar channels)
    t.meters = {
        Meter{
            MeterId{1},
            PortId{23},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Preamp 1 Out Meter",
        },
        Meter{
            MeterId{2},
            PortId{24},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Preamp 2 Out Meter",
        },
        Meter{
            MeterId{3},
            PortId{45},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out L Meter",
        },
        Meter{
            MeterId{4},
            PortId{46},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out R Meter",
        },
    };

    return t;
}

void RunDuetTopologyTests(TestContext& ctx) {
    auto duet = makeVirtualDuet();
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
            RouteBundleId{1}, // duplicate of bundle 1 in inMux
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
