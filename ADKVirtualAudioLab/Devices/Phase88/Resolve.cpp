#include "Resolve.hpp"
#include "Capabilities.hpp"

#include <algorithm>

namespace ASFW::Devices::Phase88 {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

std::expected<ResolvedAudioConfiguration, ResolveError> resolve(
    const DeviceConfiguration& config) {

    const auto& caps = capabilities();
    if (std::find(caps.sampleRates.begin(), caps.sampleRates.end(), config.sampleRate) == caps.sampleRates.end()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedSampleRate,
            "Sample rate " + std::to_string(config.sampleRate) + " is not supported by Phase88"
        });
    }

    if (config.opticalInput.has_value() || config.opticalOutput.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedOpticalMode,
            "Phase88 has no optical mode switches"
        });
    }

    ResolvedAudioConfiguration resolved;

    // 1. Streams: 10 capture channels, 10 playback channels
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{StreamDirection::Capture, 10, "Phase88 Capture (10 ch)"},
            ResolvedAudioStream{StreamDirection::Playback, 10, "Phase88 Playback (10 ch)"},
        },
    };

    // 2. Topology
    Topology& t = resolved.topology;
    t.revision = 0;

    const NodeId nPhysIn{1};
    const NodeId nHostIO{2};
    const NodeId nMixerStreamSrcSelector{3};
    const NodeId nMixer{4};
    const NodeId nOutSelector{5};
    const NodeId nPhysOut{6};

    // Pre-Mixer Stream Source Selector (FB 0x07): 5 playback pairs -> 1 selected pair
    std::vector<RouteBundle> mixerStreamSrcBundles;
    for (uint32_t pair = 0; pair < 5; ++pair) {
        mixerStreamSrcBundles.push_back(RouteBundle{
            RouteBundleId{pair + 1},
            {
                Route{PortId{41 + pair * 2}, PortId{51}},
                Route{PortId{42 + pair * 2}, PortId{52}},
            },
        });
    }

    // 12x2 Mixer Crosspoints
    std::vector<PortId> mixerInputs;
    for (uint32_t i = 1; i <= 12; ++i) mixerInputs.push_back(PortId{60 + i});
    std::vector<PortId> mixerOutputs = {PortId{75}, PortId{76}};

    std::vector<MixerCrosspoint> mixerCrosspoints;
    uint32_t cpId = 1;
    for (uint32_t i = 1; i <= 12; ++i) {
        mixerCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{60 + i}, PortId{75}});
        mixerCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{60 + i}, PortId{76}});
    }

    // Output Selector
    std::vector<PortId> selInputs;
    for (uint32_t i = 1; i <= 10; ++i) selInputs.push_back(PortId{80 + i});
    selInputs.push_back(PortId{91});
    selInputs.push_back(PortId{92});

    std::vector<PortId> selOutputs;
    for (uint32_t i = 1; i <= 10; ++i) selOutputs.push_back(PortId{92 + i});

    std::vector<RouteBundle> outSelBundles;
    uint32_t bundleId = 1;
    for (uint32_t pair = 0; pair < 5; ++pair) {
        outSelBundles.push_back(RouteBundle{
            RouteBundleId{bundleId++},
            {
                Route{PortId{81 + pair * 2}, PortId{93 + pair * 2}},
                Route{PortId{82 + pair * 2}, PortId{94 + pair * 2}},
            },
        });
    }
    for (uint32_t pair = 0; pair < 5; ++pair) {
        outSelBundles.push_back(RouteBundle{
            RouteBundleId{bundleId++},
            {
                Route{PortId{91}, PortId{93 + pair * 2}},
                Route{PortId{92}, PortId{94 + pair * 2}},
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
                    .maxDestinationsPerInput = 1,
                },
            },
        },
        Node{nPhysOut, "Physical Outputs (8 Analog + 2 SPDIF)", EndpointNode{EndpointKind::Physical}},
    };

    // Ports
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 1, "Line In " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{9}, nPhysIn, PortDirection::Output, 1, "SPDIF In L"});
    t.ports.push_back(Port{PortId{10}, nPhysIn, PortDirection::Output, 1, "SPDIF In R"});

    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostIO, PortDirection::Input, 1, "Host Capture " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{30 + i}, nHostIO, PortDirection::Output, 1, "Host Playback " + std::to_string(i)});
    }

    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nMixerStreamSrcSelector, PortDirection::Input, 1, "SrcMux In: Stream " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{51}, nMixerStreamSrcSelector, PortDirection::Output, 1, "SrcMux Out: Selected L"});
    t.ports.push_back(Port{PortId{52}, nMixerStreamSrcSelector, PortDirection::Output, 1, "SrcMux Out: Selected R"});

    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{60 + i}, nMixer, PortDirection::Input, 1, "Mixer In: Analog " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{69}, nMixer, PortDirection::Input, 1, "Mixer In: SPDIF L"});
    t.ports.push_back(Port{PortId{70}, nMixer, PortDirection::Input, 1, "Mixer In: SPDIF R"});
    t.ports.push_back(Port{PortId{71}, nMixer, PortDirection::Input, 1, "Mixer In: Stream L"});
    t.ports.push_back(Port{PortId{72}, nMixer, PortDirection::Input, 1, "Mixer In: Stream R"});
    t.ports.push_back(Port{PortId{75}, nMixer, PortDirection::Output, 1, "Mixer Out L"});
    t.ports.push_back(Port{PortId{76}, nMixer, PortDirection::Output, 1, "Mixer Out R"});

    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{80 + i}, nOutSelector, PortDirection::Input, 1, "OutMux In: Stream " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{91}, nOutSelector, PortDirection::Input, 1, "OutMux In: Mixer Out L"});
    t.ports.push_back(Port{PortId{92}, nOutSelector, PortDirection::Input, 1, "OutMux In: Mixer Out R"});
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{92 + i}, nOutSelector, PortDirection::Output, 1, "OutMux Out " + std::to_string(i)});
    }

    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{110 + i}, nPhysOut, PortDirection::Input, 1, "Line Out " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{119}, nPhysOut, PortDirection::Input, 1, "SPDIF Out L"});
    t.ports.push_back(Port{PortId{120}, nPhysOut, PortDirection::Input, 1, "SPDIF Out R"});

    // Fixed Links
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{20 + i}});
    }
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{60 + i}});
    }
    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{30 + i}, PortId{40 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{51}, PortId{71}});
    t.fixedLinks.push_back(FixedLink{PortId{52}, PortId{72}});

    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{30 + i}, PortId{80 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{75}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{76}, PortId{92}});

    for (uint32_t i = 1; i <= 10; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{92 + i}, PortId{110 + i}});
    }

    // Parameters
    t.parameters = {
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

    // Meters
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

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    state.parameters[ParameterId{1}] = false;
    state.parameters[ParameterId{2}] = false;
    state.parameters[ParameterId{3}] = 0.0;
    state.parameters[ParameterId{4}] = 0.0;
    state.parameters[ParameterId{5}] = false;
    state.parameters[ParameterId{6}] = false;
    state.parameters[ParameterId{7}] = 0.0;
    state.parameters[ParameterId{8}] = 0.0;
    state.parameters[ParameterId{9}] = int64_t{0}; // Internal clock

    // Pre-mixer Stream Source Selector (Node 3): Playback 1/2 (Bundle 1)
    state.routers[NodeId{3}] = RouterState{
        .node = NodeId{3},
        .activeBundles = {RouteBundleId{1}},
    };

    // Output Selector (Node 5): 5 direct stream playback bundles (Bundles 1..5)
    state.routers[NodeId{5}] = RouterState{
        .node = NodeId{5},
        .activeBundles = {RouteBundleId{1}, RouteBundleId{2}, RouteBundleId{3}, RouteBundleId{4}, RouteBundleId{5}},
    };

    state.meters[MeterId{1}] = -96.0;
    state.meters[MeterId{2}] = -96.0;

    return state;
}

} // namespace ASFW::Devices::Phase88
