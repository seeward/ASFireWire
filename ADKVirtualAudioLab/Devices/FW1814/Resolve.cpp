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

    const auto optIn = config.opticalInput.value_or(OpticalMode::Adat);
    const auto optOut = config.opticalOutput.value_or(OpticalMode::Adat);

    ResolvedAudioConfiguration resolved;

    const uint32_t capChannels = (optIn == OpticalMode::Adat) ? 18 : 8;
    const uint32_t playChannels = (optOut == OpticalMode::Adat) ? 14 : 8;

    // 1. Streams: 18 capture channels (or 8 with optical SPDIF), 14 playback channels (or 8 with optical SPDIF)
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{StreamDirection::Capture, capChannels, "FW1814 Capture (" + std::to_string(capChannels) + " ch)"},
            ResolvedAudioStream{StreamDirection::Playback, playChannels, "FW1814 Playback (" + std::to_string(playChannels) + " ch)"},
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
    for (uint32_t i = 1; i <= 7; ++i) {
        capInPorts.push_back(PortId{10 + i});
        capOutPorts.push_back(PortId{20 + i});
    }

    // Main Sum Mixer: 11 stereo inputs -> 1 stereo output (Main Mix 1/2) = 11 crosspoints
    std::vector<PortId> sumMixerInputs;
    for (uint32_t i = 1; i <= 11; ++i) sumMixerInputs.push_back(PortId{50 + i});
    std::vector<PortId> sumMixerOutputs = {PortId{71}};

    std::vector<MixerCrosspoint> sumCrosspoints;
    for (uint32_t src = 1; src <= 11; ++src) {
        sumCrosspoints.push_back(MixerCrosspoint{CrosspointId{src}, PortId{50 + src}, PortId{71}});
    }

    // Aux Downmix Mixer: 11 stereo inputs -> 1 stereo output (Aux Mix 3/4) = 11 crosspoints
    std::vector<PortId> auxInputs;
    for (uint32_t i = 1; i <= 11; ++i) auxInputs.push_back(PortId{100 + i});
    std::vector<PortId> auxOutputs = {PortId{112}};

    std::vector<MixerCrosspoint> auxCrosspoints;
    for (uint32_t src = 1; src <= 11; ++src) {
        auxCrosspoints.push_back(MixerCrosspoint{CrosspointId{11 + src}, PortId{100 + src}, PortId{112}});
    }

    t.nodes = {
        Node{nPhysIn, "Physical Inputs (Line 1-4, SPDIF, ADAT 1-8)", EndpointNode{EndpointKind::Physical}},
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
            "Main 11x1 Sum Mixer",
            MixerNode{
                .inputs = std::move(sumMixerInputs),
                .outputs = std::move(sumMixerOutputs),
                .crosspoints = std::move(sumCrosspoints),
            },
        },
        Node{
            nAuxMixer,
            "Aux 11x1 Downmix Mixer",
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
                .inputs = {PortId{81}, PortId{82}, PortId{83}, PortId{84}},
                .outputs = {PortId{85}, PortId{86}},
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{81}, PortId{85}}}},
                    RouteBundle{RouteBundleId{2}, {Route{PortId{82}, PortId{85}}}},
                    RouteBundle{RouteBundleId{3}, {Route{PortId{83}, PortId{85}}}},
                    RouteBundle{RouteBundleId{4}, {Route{PortId{81}, PortId{86}}}},
                    RouteBundle{RouteBundleId{5}, {Route{PortId{82}, PortId{86}}}},
                    RouteBundle{RouteBundleId{6}, {Route{PortId{84}, PortId{86}}}},
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
            "Analog Line Out Source Selectors",
            RouterNode{
                .inputs = {PortId{121}, PortId{122}, PortId{123}, PortId{124}},
                .outputs = {PortId{125}, PortId{126}},
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{121}, PortId{125}}}},
                    RouteBundle{RouteBundleId{2}, {Route{PortId{123}, PortId{125}}}},
                    RouteBundle{RouteBundleId{3}, {Route{PortId{122}, PortId{126}}}},
                    RouteBundle{RouteBundleId{4}, {Route{PortId{124}, PortId{126}}}},
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 2,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 2,
                },
            },
        },
        Node{nPhysOut, "Physical Outputs (Line 1-4, SPDIF, ADAT, HP 1/2)", EndpointNode{EndpointKind::Physical}},
    };

    // Ports (all stereo pairs = 2 channels)
    // Physical In: 1..9
    const auto opticalInKind = (optIn == OpticalMode::Adat) ? SignalKind::Adat : SignalKind::SpdifOptical;

    t.ports.push_back(endpointPort(PortId{1}, nPhysIn, PortDirection::Output, 2, {SignalKind::AnalogLine, 1}));
    t.ports.push_back(endpointPort(PortId{2}, nPhysIn, PortDirection::Output, 2, {SignalKind::AnalogLine, 3}));
    t.ports.push_back(endpointPort(PortId{3}, nPhysIn, PortDirection::Output, 2, {SignalKind::SpdifCoaxial, 1}));
    t.ports.push_back(endpointPort(PortId{4}, nPhysIn, PortDirection::Output, 2, {opticalInKind, 1}));
    // Ports 5-7 only exist in ADAT mode; optical S/PDIF carries one pair. The
    // mode-dependent structure lands with the stream-geometry rebuild, so for
    // now they stay and are visibly mislabelled in S/PDIF mode rather than
    // plausibly mislabelled as "(Off)".
    t.ports.push_back(endpointPort(PortId{5}, nPhysIn, PortDirection::Output, 2, {SignalKind::Adat, 3}));
    t.ports.push_back(endpointPort(PortId{6}, nPhysIn, PortDirection::Output, 2, {SignalKind::Adat, 5}));
    t.ports.push_back(endpointPort(PortId{7}, nPhysIn, PortDirection::Output, 2, {SignalKind::Adat, 7}));

    for (uint32_t i = 1; i <= 7; ++i) {
        t.ports.push_back(Port{PortId{10 + i}, nHostCaptureBus, PortDirection::Input, 2, "Capture Bus In " + std::to_string(i)});
        t.ports.push_back(Port{PortId{20 + i}, nHostCaptureBus, PortDirection::Output, 2, "Capture Bus Out " + std::to_string(i)});
        t.ports.push_back(endpointPort(PortId{30 + i}, nHostIO, PortDirection::Input, 2, {SignalKind::HostStream, i * 2 - 1}));
    }

    for (uint32_t i = 1; i <= 7; ++i) {
        t.ports.push_back(endpointPort(PortId{40 + i}, nHostIO, PortDirection::Output, 2, {SignalKind::HostStream, i * 2 - 1}));
    }

    // Main Sum Mixer Inputs: 51..61
    const std::vector<std::string> chNames = {
        "Line In 1/2", "Line In 3/4", "S/PDIF In",
        (optIn == OpticalMode::Adat) ? "ADAT In 1/2" : "Opt SPDIF In",
        (optIn == OpticalMode::Adat) ? "ADAT In 3/4" : "Opt In 3/4 (Off)",
        (optIn == OpticalMode::Adat) ? "ADAT In 5/6" : "Opt In 5/6 (Off)",
        (optIn == OpticalMode::Adat) ? "ADAT In 7/8" : "Opt In 7/8 (Off)",
        "DAW Playback 1/2", "DAW Playback 3/4", "DAW Playback 5/6", "DAW Playback 7/8"
    };

    for (uint32_t i = 1; i <= 11; ++i) {
        t.ports.push_back(Port{PortId{50 + i}, nSumMixer, PortDirection::Input, 2, "Main In: " + chNames[i - 1]});
        t.ports.push_back(Port{PortId{100 + i}, nAuxMixer, PortDirection::Input, 2, "Aux In: " + chNames[i - 1]});
    }
    t.ports.push_back(Port{PortId{71}, nSumMixer, PortDirection::Output, 2, "Main Mix 1/2"});
    t.ports.push_back(Port{PortId{112}, nAuxMixer, PortDirection::Output, 2, "Aux Mix 3/4"});

    // HP Router Ports
    t.ports.push_back(Port{PortId{81}, nHpMux, PortDirection::Input, 2, "Main Mix 1/2"});
    t.ports.push_back(Port{PortId{82}, nHpMux, PortDirection::Input, 2, "Aux Mix 3/4"});
    t.ports.push_back(Port{PortId{83}, nHpMux, PortDirection::Input, 2, "DAW Playback 1/2"});
    t.ports.push_back(Port{PortId{84}, nHpMux, PortDirection::Input, 2, "DAW Playback 3/4"});
    t.ports.push_back(Port{PortId{85}, nHpMux, PortDirection::Output, 2, "Headphone 1 (A)"});
    t.ports.push_back(Port{PortId{86}, nHpMux, PortDirection::Output, 2, "Headphone 2 (B)"});

    // Line Out Router Ports
    t.ports.push_back(Port{PortId{121}, nLineOutMux, PortDirection::Input, 2, "Main Mix 1/2"});
    t.ports.push_back(Port{PortId{122}, nLineOutMux, PortDirection::Input, 2, "Aux Mix 3/4"});
    t.ports.push_back(Port{PortId{123}, nLineOutMux, PortDirection::Input, 2, "DAW Playback 1/2"});
    t.ports.push_back(Port{PortId{124}, nLineOutMux, PortDirection::Input, 2, "DAW Playback 3/4"});
    t.ports.push_back(Port{PortId{125}, nLineOutMux, PortDirection::Output, 2, "Line Out 1/2"});
    t.ports.push_back(Port{PortId{126}, nLineOutMux, PortDirection::Output, 2, "Line Out 3/4"});

    // Physical Outputs
    t.ports.push_back(endpointPort(PortId{91}, nPhysOut, PortDirection::Input, 2, {SignalKind::AnalogLine, 1}));
    t.ports.push_back(endpointPort(PortId{92}, nPhysOut, PortDirection::Input, 2, {SignalKind::AnalogLine, 3}));
    t.ports.push_back(endpointPort(PortId{93}, nPhysOut, PortDirection::Input, 2, {SignalKind::SpdifCoaxial, 1}));
    t.ports.push_back(endpointPort(PortId{98}, nPhysOut, PortDirection::Input, 2, {SignalKind::Headphone, 1}));
    t.ports.push_back(endpointPort(PortId{99}, nPhysOut, PortDirection::Input, 2, {SignalKind::Headphone, 3}));

    // Fixed Links
    for (uint32_t i = 1; i <= 7; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{10 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{20 + i}, PortId{30 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{50 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{100 + i}});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{40 + i}, PortId{57 + i}});
        t.fixedLinks.push_back(FixedLink{PortId{40 + i}, PortId{107 + i}});
    }

    t.fixedLinks.push_back(FixedLink{PortId{43}, PortId{93}}); // Direct SPDIF Playback

    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{121}});
    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{122}});
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{123}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{124}});

    t.fixedLinks.push_back(FixedLink{PortId{71}, PortId{81}});
    t.fixedLinks.push_back(FixedLink{PortId{112}, PortId{82}});
    t.fixedLinks.push_back(FixedLink{PortId{41}, PortId{83}});
    t.fixedLinks.push_back(FixedLink{PortId{42}, PortId{84}});

    t.fixedLinks.push_back(FixedLink{PortId{125}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{126}, PortId{92}});
    t.fixedLinks.push_back(FixedLink{PortId{85}, PortId{98}});
    t.fixedLinks.push_back(FixedLink{PortId{86}, PortId{99}});

    // 3. Audio Semantics: Logical Channels (11) & Busses (2)
    for (uint32_t i = 1; i <= 7; ++i) {
        t.channels.push_back(Channel{
            .id = ChannelId{i},
            .name = chNames[i - 1],
            .ports = {PortId{i}, PortId{50 + i}, PortId{100 + i}},
        });
    }
    for (uint32_t i = 8; i <= 11; ++i) {
        t.channels.push_back(Channel{
            .id = ChannelId{i},
            .name = chNames[i - 1],
            .ports = {PortId{40 + i - 7}, PortId{50 + i}, PortId{100 + i}},
        });
    }

    t.buses = {
        Bus{
            .id = BusId{1},
            .semantic = BusSemantic::Main,
            .name = "Main Mix 1/2",
            .ports = {PortId{71}},
        },
        Bus{
            .id = BusId{2},
            .semantic = BusSemantic::Aux,
            .name = "Aux Mix 3/4",
            .ports = {PortId{112}},
        },
    };

    // 4. Parameters
    // 11 Main Sends (CrosspointId 1..11)
    for (uint32_t i = 1; i <= 11; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{i},
            .target = CrosspointId{i},
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Main Send",
        });
    }

    // 11 Aux Sends (CrosspointId 12..22)
    for (uint32_t i = 1; i <= 11; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{11 + i},
            .target = CrosspointId{11 + i},
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Aux Send",
        });
    }

    // 11 Pan Controls (PortId 51..61)
    for (uint32_t i = 1; i <= 11; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{22 + i},
            .target = PortId{50 + i},
            .semantic = ParameterSemantic::Pan,
            .domain = ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            .name = chNames[i - 1] + " Pan",
        });
    }

    // 11 Mute Controls (PortId 51..61)
    for (uint32_t i = 1; i <= 11; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{33 + i},
            .target = PortId{50 + i},
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = chNames[i - 1] + " Mute",
        });
    }

    // 11 Solo Controls (PortId 51..61)
    for (uint32_t i = 1; i <= 11; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{44 + i},
            .target = PortId{50 + i},
            .semantic = ParameterSemantic::Solo,
            .domain = BooleanDomain{},
            .name = chNames[i - 1] + " Solo",
        });
    }

    // 5 Output Masters (Level + Mute)
    const std::vector<std::pair<PortId, std::string>> outMasters = {
        {PortId{91}, "Analog Out 1/2"},
        {PortId{92}, "Analog Out 3/4"},
        {PortId{98}, "Headphone 1 (A)"},
        {PortId{99}, "Headphone 2 (B)"},
        {PortId{93}, "S/PDIF Out"},
    };

    uint32_t pId = 56;
    for (const auto& [port, name] : outMasters) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = port,
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = name + " Level",
        });
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = port,
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = name + " Mute",
        });
    }

    const auto pClock = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pClock,
        .target = nPhysIn,
        .semantic = ParameterSemantic::ClockSource,
        .domain = EnumDomain{
            .values = {
                EnumItem{0, "Internal"},
                EnumItem{1, "S/PDIF Coaxial"},
                EnumItem{2, "ADAT Optical"},
                EnumItem{3, "Word Clock BNC"},
            },
        },
        .name = "Clock Source",
    });

    // 5. Meters (11 Channel Meters + 5 Output Meters)
    for (uint32_t i = 1; i <= 7; ++i) {
        t.meters.push_back(Meter{
            .id = MeterId{i},
            .target = PortId{i},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Peak Meter",
        });
    }
    for (uint32_t i = 8; i <= 11; ++i) {
        t.meters.push_back(Meter{
            .id = MeterId{i},
            .target = PortId{40 + i - 7},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Peak Meter",
        });
    }
    uint32_t mId = 12;
    for (const auto& [port, name] : outMasters) {
        t.meters.push_back(Meter{
            .id = MeterId{mId++},
            .target = port,
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = name + " Peak Meter",
        });
    }

    // 6. Presentation Hints (Thin presentation hints only)
    resolved.presentation.routers = {
        Presentation::RouterPresentationHint{
            .router = NodeId{6},
            .style = Presentation::RouterPresentationStyle::Selector,
            .bundleGroups = {
                Presentation::RouteBundleGroup{
                    .name = "Headphone 1 (A) Source",
                    .bundles = {RouteBundleId{1}, RouteBundleId{2}, RouteBundleId{3}},
                },
                Presentation::RouteBundleGroup{
                    .name = "Headphone 2 (B) Source",
                    .bundles = {RouteBundleId{4}, RouteBundleId{5}, RouteBundleId{6}},
                },
            },
        },
        Presentation::RouterPresentationHint{
            .router = NodeId{7},
            .style = Presentation::RouterPresentationStyle::Selector,
            .bundleGroups = {
                Presentation::RouteBundleGroup{
                    .name = "Line Out 1/2 Source",
                    .bundles = {RouteBundleId{1}, RouteBundleId{2}},
                },
                Presentation::RouteBundleGroup{
                    .name = "Line Out 3/4 Source",
                    .bundles = {RouteBundleId{3}, RouteBundleId{4}},
                },
            },
        },
    };

    resolved.presentation.mixers = {
        Presentation::MixerPresentationHint{
            .mixer = NodeId{4},
            .style = Presentation::MixerPresentationStyle::ChannelStrips,
        },
        Presentation::MixerPresentationHint{
            .mixer = NodeId{5},
            .style = Presentation::MixerPresentationStyle::ChannelStrips,
        },
    };

    // Parameter Presentation Hints (Fader for Main, Rotary for Aux & Pan, Toggle for Mute & Solo)
    for (uint32_t i = 1; i <= 11; ++i) {
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = ParameterId{i},
            .presentation = Presentation::ControlPresentation::Fader,
        });
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = ParameterId{11 + i},
            .presentation = Presentation::ControlPresentation::Rotary,
        });
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = ParameterId{22 + i},
            .presentation = Presentation::ControlPresentation::Rotary,
        });
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = ParameterId{33 + i},
            .presentation = Presentation::ControlPresentation::Toggle,
        });
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = ParameterId{44 + i},
            .presentation = Presentation::ControlPresentation::Toggle,
        });
    }

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    // Default Main Sends: Unity (0.0 dB) for DAW Playback 1/2 and Line In 1/2, -128 dB for others
    for (uint32_t i = 1; i <= 11; ++i) {
        state.parameters[ParameterId{i}] = (i == 1 || i == 8) ? 0.0 : -128.0;
        state.parameters[ParameterId{11 + i}] = -128.0; // Aux sends at -inf
        state.parameters[ParameterId{22 + i}] = 0.0;    // Pan center
        state.parameters[ParameterId{33 + i}] = false;  // Unmuted
        state.parameters[ParameterId{44 + i}] = false;  // Unsoloed
    }

    // Output Masters (56..65)
    for (uint32_t p = 56; p < 66; p += 2) {
        state.parameters[ParameterId{p}] = 0.0;      // Level 0.0 dB
        state.parameters[ParameterId{p + 1}] = false; // Unmuted
    }

    // Clock Source (ParameterId 66)
    state.parameters[ParameterId{66}] = int64_t{0}; // Internal Clock

    // Headphone Mux (Node 6): HP 1 <- Mix 0 (Bundle 1), HP 2 <- Mix 1 (Bundle 5)
    state.routers[NodeId{6}] = RouterState{
        .activeBundles = {RouteBundleId{1}, RouteBundleId{5}},
    };

    // LineOut Mux (Node 7): LineOut 1/2 <- Mix 0 (Bundle 1), LineOut 3/4 <- Mix 1 (Bundle 3)
    state.routers[NodeId{7}] = RouterState{
        .activeBundles = {RouteBundleId{1}, RouteBundleId{3}},
    };

    for (uint32_t m = 1; m <= 16; ++m) {
        state.meters[MeterId{m}] = -128.0;
    }

    return state;
}

} // namespace ASFW::Devices::FW1814
