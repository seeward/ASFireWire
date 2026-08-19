#include "Resolve.hpp"
#include "Capabilities.hpp"

#include <algorithm>

namespace ASFW::Devices::FW1814 {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

std::expected<ResolvedAudioConfiguration, ResolveError> resolve(
    const DeviceConfiguration& config) {

    const auto& caps = capabilities();
    if (std::find(caps.sampleRates.begin(), caps.sampleRates.end(), config.sampleRate) == caps.sampleRates.end()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedSampleRate,
            "Sample rate " + std::to_string(config.sampleRate) + " is not supported by FW1814"
        });
    }

    if (config.opticalInput.has_value() || config.opticalOutput.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedOpticalMode,
            "FW1814 has no optical mode switches"
        });
    }

    ResolvedAudioConfiguration resolved;

    // 1. Streams: 18 capture channels (9 stereo pairs), 14 playback channels (7 stereo pairs)
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{StreamDirection::Capture, 18, "FW1814 Capture (18 ch)"},
            ResolvedAudioStream{StreamDirection::Playback, 14, "FW1814 Playback (14 ch)"},
        },
    };

    // 2. Topology
    Topology& t = resolved.topology;
    t.revision = 0;

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

    // Main Sum Mixer: 22x4 (11 stereo inputs x 2 stereo outputs = 22 crosspoints)
    std::vector<PortId> sumMixerInputs;
    for (uint32_t i = 1; i <= 11; ++i) sumMixerInputs.push_back(PortId{50 + i});
    std::vector<PortId> sumMixerOutputs = {PortId{71}, PortId{72}};

    std::vector<MixerCrosspoint> sumCrosspoints;
    uint32_t cpId = 1;
    for (uint32_t src = 1; src <= 11; ++src) {
        sumCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{50 + src}, PortId{71}});
        sumCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{50 + src}, PortId{72}});
    }

    // Aux Downmix Mixer: 22x2 (11 stereo inputs x 1 stereo output = 11 crosspoints)
    std::vector<PortId> auxInputs;
    for (uint32_t i = 1; i <= 11; ++i) auxInputs.push_back(PortId{100 + i});
    std::vector<PortId> auxOutputs = {PortId{112}};

    std::vector<MixerCrosspoint> auxCrosspoints;
    for (uint32_t src = 1; src <= 11; ++src) {
        auxCrosspoints.push_back(MixerCrosspoint{CrosspointId{cpId++}, PortId{100 + src}, PortId{112}});
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
                .inputs = {PortId{81}, PortId{82}, PortId{83}},
                .outputs = {PortId{84}, PortId{85}},
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{81}, PortId{84}}}},
                    RouteBundle{RouteBundleId{2}, {Route{PortId{82}, PortId{84}}}},
                    RouteBundle{RouteBundleId{3}, {Route{PortId{83}, PortId{84}}}},
                    RouteBundle{RouteBundleId{4}, {Route{PortId{81}, PortId{85}}}},
                    RouteBundle{RouteBundleId{5}, {Route{PortId{82}, PortId{85}}}},
                    RouteBundle{RouteBundleId{6}, {Route{PortId{83}, PortId{85}}}},
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
                .inputs = {PortId{121}, PortId{122}, PortId{123}},
                .outputs = {PortId{124}, PortId{125}},
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{121}, PortId{124}}}},
                    RouteBundle{RouteBundleId{2}, {Route{PortId{123}, PortId{124}}}},
                    RouteBundle{RouteBundleId{3}, {Route{PortId{122}, PortId{125}}}},
                    RouteBundle{RouteBundleId{4}, {Route{PortId{123}, PortId{125}}}},
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
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 2, "Line In " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{5}, nPhysIn, PortDirection::Output, 2, "SPDIF In"});
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{5 + i}, nPhysIn, PortDirection::Output, 2, "ADAT In " + std::to_string(i)});
    }

    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{10 + i}, nHostCaptureBus, PortDirection::Input, 2, "I18S Bus In Pair " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostCaptureBus, PortDirection::Output, 2, "I18S Bus Out Pair " + std::to_string(i)});
    }

    for (uint32_t i = 1; i <= 9; ++i) {
        t.ports.push_back(Port{PortId{30 + i}, nHostIO, PortDirection::Input, 2, "Host Capture Pair " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 7; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nHostIO, PortDirection::Output, 2, "Host Playback Stream " + std::to_string(i * 2 - 1) + "/" + std::to_string(i * 2)});
    }

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

    t.ports.push_back(Port{PortId{81}, nHpMux, PortDirection::Input, 2, "HpMux In: Mix 0"});
    t.ports.push_back(Port{PortId{82}, nHpMux, PortDirection::Input, 2, "HpMux In: Mix 1"});
    t.ports.push_back(Port{PortId{83}, nHpMux, PortDirection::Input, 2, "HpMux In: Aux 0"});
    t.ports.push_back(Port{PortId{84}, nHpMux, PortDirection::Output, 2, "HpMux Out: HP 1"});
    t.ports.push_back(Port{PortId{85}, nHpMux, PortDirection::Output, 2, "HpMux Out: HP 2"});

    t.ports.push_back(Port{PortId{121}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Mix 0"});
    t.ports.push_back(Port{PortId{122}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Mix 1"});
    t.ports.push_back(Port{PortId{123}, nLineOutMux, PortDirection::Input, 2, "LineMux In: Aux 0"});
    t.ports.push_back(Port{PortId{124}, nLineOutMux, PortDirection::Output, 2, "LineMux Out: LineOut 1/2"});
    t.ports.push_back(Port{PortId{125}, nLineOutMux, PortDirection::Output, 2, "LineMux Out: LineOut 3/4"});

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
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{10 + i}});
    }
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{20 + i}, PortId{30 + i}});
    }
    for (uint32_t i = 1; i <= 9; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{50 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{100 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{60}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{61}});
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{110}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{111}});

    t.fixedLinks.push_back(FixedLink{PortId{43}, PortId{93}});
    t.fixedLinks.push_back(FixedLink{PortId{44}, PortId{94}});
    t.fixedLinks.push_back(FixedLink{PortId{45}, PortId{95}});
    t.fixedLinks.push_back(FixedLink{PortId{46}, PortId{96}});
    t.fixedLinks.push_back(FixedLink{PortId{47}, PortId{97}});

    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{121}});
    t.fixedLinks.push_back(FixedLink{PortId{72}, PortId{122}});
    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{81}});
    t.fixedLinks.push_back(FixedLink{PortId{72}, PortId{82}});

    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{123}});
    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{83}});

    t.fixedLinks.push_back(FixedLink{PortId{124}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{125}, PortId{92}});

    t.fixedLinks.push_back(FixedLink{PortId{84}, PortId{98}});
    t.fixedLinks.push_back(FixedLink{PortId{85}, PortId{99}});

    // Parameters
    t.parameters = {
        Parameter{
            ParameterId{1},
            PortId{60},
            ParameterSemantic::Level,
            ScalarDomain{.min = -128.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Stream Playback 1/2 Input Gain",
        },
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

    // Meters
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

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    state.parameters[ParameterId{1}] = 0.0;
    state.parameters[ParameterId{2}] = 0.0;
    state.parameters[ParameterId{3}] = 0.0;
    state.parameters[ParameterId{4}] = 0.0;
    state.parameters[ParameterId{5}] = 0.0;

    // Headphone Mux (Node 6): HP 1 <- Mix 0 (Bundle 1), HP 2 <- Mix 1 (Bundle 5)
    state.routers[NodeId{6}] = RouterState{
        .node = NodeId{6},
        .activeBundles = {RouteBundleId{1}, RouteBundleId{5}},
    };

    // LineOut Mux (Node 7): LineOut 1/2 <- Mix 0 (Bundle 1), LineOut 3/4 <- Mix 1 (Bundle 3)
    state.routers[NodeId{7}] = RouterState{
        .node = NodeId{7},
        .activeBundles = {RouteBundleId{1}, RouteBundleId{3}},
    };

    state.meters[MeterId{1}] = -128.0;
    state.meters[MeterId{2}] = -128.0;
    state.meters[MeterId{3}] = -128.0;

    return state;
}

} // namespace ASFW::Devices::FW1814
