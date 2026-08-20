#include "Resolve.hpp"
#include "Capabilities.hpp"

#include <algorithm>

namespace ASFW::Devices::Duet {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

std::expected<ResolvedAudioConfiguration, ResolveError> resolve(
    const DeviceConfiguration& config) {

    const auto& caps = capabilities();
    if (std::find(caps.sampleRates.begin(), caps.sampleRates.end(), config.sampleRate) == caps.sampleRates.end()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedSampleRate,
            "Sample rate " + std::to_string(config.sampleRate) + " is not supported by Duet"
        });
    }

    if (config.opticalInput.has_value() || config.opticalOutput.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedOpticalMode,
            "Duet has no optical interface"
        });
    }

    ResolvedAudioConfiguration resolved;

    // 1. Resolved Streams
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{StreamDirection::Capture, 2, "Duet Capture (2 ch)"},
            ResolvedAudioStream{StreamDirection::Playback, 2, "Duet Playback (2 ch)"},
        },
    };

    // 2. Topology
    Topology& t = resolved.topology;
    t.revision = 0; // Runtime will assign the revision upon publication

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
                    MixerCrosspoint{CrosspointId{1}, PortId{41}, PortId{45}}, // Analog 1 -> Mixer Master
                    MixerCrosspoint{CrosspointId{2}, PortId{42}, PortId{45}}, // Analog 2 -> Mixer Master
                    MixerCrosspoint{CrosspointId{3}, PortId{43}, PortId{45}}, // Playback 1 -> Mixer Master
                    MixerCrosspoint{CrosspointId{4}, PortId{44}, PortId{45}}, // Playback 2 -> Mixer Master
                },
            },
        },
        Node{
            nOutMux,
            "Output Source Router",
            RouterNode{
                .inputs = {PortId{51}, PortId{52}, PortId{53}, PortId{54}},
                .outputs = {PortId{55}, PortId{56}},
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
        endpointPort(PortId{1}, nPhysIn, PortDirection::Output, 1, {SignalKind::AnalogMicXlr, 1}),
        endpointPort(PortId{2}, nPhysIn, PortDirection::Output, 1, {SignalKind::AnalogMicXlr, 2}),
        endpointPort(PortId{3}, nPhysIn, PortDirection::Output, 1, {SignalKind::AnalogInstrument, 1}),
        endpointPort(PortId{4}, nPhysIn, PortDirection::Output, 1, {SignalKind::AnalogInstrument, 2}),

        Port{PortId{11}, nInMux, PortDirection::Input, 1, "Mux In: XLR 1"},
        Port{PortId{12}, nInMux, PortDirection::Input, 1, "Mux In: Inst 1"},
        Port{PortId{13}, nInMux, PortDirection::Input, 1, "Mux In: XLR 2"},
        Port{PortId{14}, nInMux, PortDirection::Input, 1, "Mux In: Inst 2"},
        Port{PortId{15}, nInMux, PortDirection::Output, 1, "Mux Out: Selected 1"},
        Port{PortId{16}, nInMux, PortDirection::Output, 1, "Mux Out: Selected 2"},

        Port{PortId{21}, nPreamp, PortDirection::Input, 1, "Preamp In 1"},
        Port{PortId{22}, nPreamp, PortDirection::Input, 1, "Preamp In 2"},
        Port{PortId{23}, nPreamp, PortDirection::Output, 1, "Preamp Out 1"},
        Port{PortId{24}, nPreamp, PortDirection::Output, 1, "Preamp Out 2"},

        endpointPort(PortId{31}, nHostIO, PortDirection::Input, 1, {SignalKind::HostStream, 1}),
        endpointPort(PortId{32}, nHostIO, PortDirection::Input, 1, {SignalKind::HostStream, 2}),
        endpointPort(PortId{33}, nHostIO, PortDirection::Output, 1, {SignalKind::HostStream, 1}),
        endpointPort(PortId{34}, nHostIO, PortDirection::Output, 1, {SignalKind::HostStream, 2}),

        Port{PortId{41}, nMixer, PortDirection::Input, 1, "Mixer In: Analog 1"},
        Port{PortId{42}, nMixer, PortDirection::Input, 1, "Mixer In: Analog 2"},
        Port{PortId{43}, nMixer, PortDirection::Input, 1, "Mixer In: Playback 1"},
        Port{PortId{44}, nMixer, PortDirection::Input, 1, "Mixer In: Playback 2"},
        Port{PortId{45}, nMixer, PortDirection::Output, 1, "Mixer Out L"},
        Port{PortId{46}, nMixer, PortDirection::Output, 1, "Mixer Out R"},

        Port{PortId{51}, nOutMux, PortDirection::Input, 1, "OutMux In: Playback 1"},
        Port{PortId{52}, nOutMux, PortDirection::Input, 1, "OutMux In: Playback 2"},
        Port{PortId{53}, nOutMux, PortDirection::Input, 1, "OutMux In: Mixer Out L"},
        Port{PortId{54}, nOutMux, PortDirection::Input, 1, "OutMux In: Mixer Out R"},
        Port{PortId{55}, nOutMux, PortDirection::Output, 1, "OutMux Out: Selected L"},
        Port{PortId{56}, nOutMux, PortDirection::Output, 1, "OutMux Out: Selected R"},

        Port{PortId{57}, nOutStage, PortDirection::Input, 1, "OutStage In L"},
        Port{PortId{58}, nOutStage, PortDirection::Input, 1, "OutStage In R"},
        Port{PortId{59}, nOutStage, PortDirection::Output, 1, "OutStage Out L"},
        Port{PortId{60}, nOutStage, PortDirection::Output, 1, "OutStage Out R"},

        endpointPort(PortId{61}, nPhysOut, PortDirection::Input, 1, {SignalKind::AnalogLine, 1}),
        endpointPort(PortId{62}, nPhysOut, PortDirection::Input, 1, {SignalKind::AnalogLine, 2}),
        endpointPort(PortId{63}, nPhysOut, PortDirection::Input, 1, {SignalKind::Headphone, 1}),
        endpointPort(PortId{64}, nPhysOut, PortDirection::Input, 1, {SignalKind::Headphone, 2}),
    };

    // Fixed Links
    t.fixedLinks = {
        FixedLink{PortId{1}, PortId{11}},
        FixedLink{PortId{3}, PortId{12}},
        FixedLink{PortId{2}, PortId{13}},
        FixedLink{PortId{4}, PortId{14}},

        FixedLink{PortId{15}, PortId{21}},
        FixedLink{PortId{16}, PortId{22}},

        FixedLink{PortId{23}, PortId{31}},
        FixedLink{PortId{24}, PortId{32}},

        FixedLink{PortId{23}, PortId{41}},
        FixedLink{PortId{24}, PortId{42}},

        FixedLink{PortId{33}, PortId{43}},
        FixedLink{PortId{34}, PortId{44}},

        FixedLink{PortId{33}, PortId{51}},
        FixedLink{PortId{34}, PortId{52}},

        FixedLink{PortId{45}, PortId{53}},
        FixedLink{PortId{46}, PortId{54}},

        FixedLink{PortId{55}, PortId{57}},
        FixedLink{PortId{56}, PortId{58}},

        FixedLink{PortId{59}, PortId{61}},
        FixedLink{PortId{60}, PortId{62}},

        FixedLink{PortId{59}, PortId{63}},
        FixedLink{PortId{60}, PortId{64}},
    };

    // 3. Audio Semantics: Logical Channels & Buses
    t.channels = {
        Channel{
            .id = ChannelId{1},
            .name = "Input 1",
            .ports = {PortId{1}, PortId{3}, PortId{15}, PortId{21}, PortId{23}, PortId{41}},
        },
        Channel{
            .id = ChannelId{2},
            .name = "Input 2",
            .ports = {PortId{2}, PortId{4}, PortId{16}, PortId{22}, PortId{24}, PortId{42}},
        },
        Channel{
            .id = ChannelId{3},
            .name = "DAW Playback 1",
            .ports = {PortId{33}, PortId{43}},
        },
        Channel{
            .id = ChannelId{4},
            .name = "DAW Playback 2",
            .ports = {PortId{34}, PortId{44}},
        },
    };

    t.buses = {
        Bus{
            .id = BusId{1},
            .semantic = BusSemantic::Main,
            .name = "Mixer Master L/R",
            .ports = {PortId{45}, PortId{46}},
        },
        Bus{
            .id = BusId{2},
            .semantic = BusSemantic::Monitor,
            .name = "Main Out (Encoder)",
            .ports = {PortId{59}, PortId{60}},
        },
    };

    // Parameters
    t.parameters = {
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
        Parameter{
            ParameterId{9},
            PortId{59},
            ParameterSemantic::Level,
            ScalarDomain{.min = -64.0, .max = 0.0, .step = 1.0, .unit = ScalarUnit::Decibels},
            "Main Out Volume",
        },
        Parameter{
            ParameterId{10},
            PortId{59},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Main Out Mute",
        },
        // Mixer Send Levels (Crosspoints 1..4)
        Parameter{
            ParameterId{11},
            CrosspointId{1},
            ParameterSemantic::Level,
            ScalarDomain{.min = -48.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Input 1 Mixer Send",
        },
        Parameter{
            ParameterId{12},
            CrosspointId{2},
            ParameterSemantic::Level,
            ScalarDomain{.min = -48.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Input 2 Mixer Send",
        },
        Parameter{
            ParameterId{13},
            CrosspointId{3},
            ParameterSemantic::Level,
            ScalarDomain{.min = -48.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "DAW 1 Mixer Send",
        },
        Parameter{
            ParameterId{14},
            CrosspointId{4},
            ParameterSemantic::Level,
            ScalarDomain{.min = -48.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "DAW 2 Mixer Send",
        },
        // Mixer Pan pots
        Parameter{
            ParameterId{15},
            PortId{41},
            ParameterSemantic::Pan,
            ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            "Input 1 Pan",
        },
        Parameter{
            ParameterId{16},
            PortId{42},
            ParameterSemantic::Pan,
            ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            "Input 2 Pan",
        },
        Parameter{
            ParameterId{17},
            PortId{43},
            ParameterSemantic::Pan,
            ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            "DAW 1 Pan",
        },
        Parameter{
            ParameterId{18},
            PortId{44},
            ParameterSemantic::Pan,
            ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            "DAW 2 Pan",
        },
        // Mixer Mutes
        Parameter{
            ParameterId{19},
            PortId{41},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Input 1 Mute",
        },
        Parameter{
            ParameterId{20},
            PortId{42},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Input 2 Mute",
        },
        Parameter{
            ParameterId{21},
            PortId{43},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "DAW 1 Mute",
        },
        Parameter{
            ParameterId{22},
            PortId{44},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "DAW 2 Mute",
        },
        // Mixer Solos
        Parameter{
            ParameterId{23},
            PortId{41},
            ParameterSemantic::Solo,
            BooleanDomain{},
            "Input 1 Solo",
        },
        Parameter{
            ParameterId{24},
            PortId{42},
            ParameterSemantic::Solo,
            BooleanDomain{},
            "Input 2 Solo",
        },
        Parameter{
            ParameterId{25},
            PortId{43},
            ParameterSemantic::Solo,
            BooleanDomain{},
            "DAW 1 Solo",
        },
        Parameter{
            ParameterId{26},
            PortId{44},
            ParameterSemantic::Solo,
            BooleanDomain{},
            "DAW 2 Solo",
        },
        // Master Section Parameters
        Parameter{
            ParameterId{27},
            PortId{45},
            ParameterSemantic::Level,
            ScalarDomain{.min = -48.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            "Mixer Master Level",
        },
        Parameter{
            ParameterId{28},
            PortId{45},
            ParameterSemantic::Mute,
            BooleanDomain{},
            "Mixer Master Mute",
        },
        Parameter{
            ParameterId{29},
            PortId{59},
            ParameterSemantic::Dim,
            BooleanDomain{},
            "Main Out Dim",
        },
    };

    // Meters
    t.meters = {
        Meter{
            MeterId{1},
            PortId{23},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Input 1 Meter",
        },
        Meter{
            MeterId{2},
            PortId{24},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Input 2 Meter",
        },
        Meter{
            MeterId{3},
            PortId{43},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "DAW 1 Meter",
        },
        Meter{
            MeterId{4},
            PortId{44},
            MeterSemantic::Level,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "DAW 2 Meter",
        },
        Meter{
            MeterId{5},
            PortId{45},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out L Meter",
        },
        Meter{
            MeterId{6},
            PortId{46},
            MeterSemantic::Peak,
            ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            "Mixer Out R Meter",
        },
    };

    // Presentation Metadata
    resolved.presentation.routers = {
        Presentation::RouterPresentationHint{
            .router = NodeId{2},
            .style = Presentation::RouterPresentationStyle::Selector,
        },
        Presentation::RouterPresentationHint{
            .router = NodeId{6},
            .style = Presentation::RouterPresentationStyle::Selector,
        },
    };

    resolved.presentation.mixers = {
        Presentation::MixerPresentationHint{
            .mixer = NodeId{5},
            .style = Presentation::MixerPresentationStyle::ChannelStrips,
        },
    };

    resolved.presentation.parameters = {
        {.parameter = ParameterId{1}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{2}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{3}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{4}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{5}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{6}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{7}, .presentation = Presentation::ControlPresentation::Selector},
        {.parameter = ParameterId{8}, .presentation = Presentation::ControlPresentation::Selector},
        {.parameter = ParameterId{9}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{10}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{11}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{12}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{13}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{14}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{15}, .presentation = Presentation::ControlPresentation::Rotary},
        {.parameter = ParameterId{16}, .presentation = Presentation::ControlPresentation::Rotary},
        {.parameter = ParameterId{17}, .presentation = Presentation::ControlPresentation::Rotary},
        {.parameter = ParameterId{18}, .presentation = Presentation::ControlPresentation::Rotary},
        {.parameter = ParameterId{19}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{20}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{21}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{22}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{23}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{24}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{25}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{26}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{27}, .presentation = Presentation::ControlPresentation::Fader},
        {.parameter = ParameterId{28}, .presentation = Presentation::ControlPresentation::Toggle},
        {.parameter = ParameterId{29}, .presentation = Presentation::ControlPresentation::Toggle},
    };

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    // Default parameter values
    state.parameters[ParameterId{1}] = 20.0;  // Input 1 gain: 20dB
    state.parameters[ParameterId{2}] = 20.0;  // Input 2 gain: 20dB
    state.parameters[ParameterId{3}] = false; // Phantom 1 off
    state.parameters[ParameterId{4}] = false; // Phantom 2 off
    state.parameters[ParameterId{5}] = false; // Phase invert 1 off
    state.parameters[ParameterId{6}] = false; // Phase invert 2 off
    state.parameters[ParameterId{7}] = int64_t{0}; // Mic mode
    state.parameters[ParameterId{8}] = int64_t{0}; // Mic mode
    state.parameters[ParameterId{9}] = 0.0;   // Master Vol: 0dB
    state.parameters[ParameterId{10}] = false;// Master Mute: unmuted

    // Mixer sends: default 0.0 dB
    state.parameters[ParameterId{11}] = 0.0;
    state.parameters[ParameterId{12}] = 0.0;
    state.parameters[ParameterId{13}] = 0.0;
    state.parameters[ParameterId{14}] = 0.0;

    // Mixer pans: Input 1/2 center (0%), DAW 1 hard left (-100%), DAW 2 hard right (+100%)
    state.parameters[ParameterId{15}] = 0.0;
    state.parameters[ParameterId{16}] = 0.0;
    state.parameters[ParameterId{17}] = -100.0;
    state.parameters[ParameterId{18}] = 100.0;

    // Mixer mutes / solos: false
    state.parameters[ParameterId{19}] = false;
    state.parameters[ParameterId{20}] = false;
    state.parameters[ParameterId{21}] = false;
    state.parameters[ParameterId{22}] = false;
    state.parameters[ParameterId{23}] = false;
    state.parameters[ParameterId{24}] = false;
    state.parameters[ParameterId{25}] = false;
    state.parameters[ParameterId{26}] = false;

    // Mixer Master & Dim
    state.parameters[ParameterId{27}] = 0.0;
    state.parameters[ParameterId{28}] = false;
    state.parameters[ParameterId{29}] = false;

    // Default Router States:
    // Input router (Node 2): XLR 1 (Bundle 1) + XLR 2 (Bundle 3) active
    state.routers[NodeId{2}] = RouterState{
        .activeBundles = {RouteBundleId{1}, RouteBundleId{3}},
    };

    // Output router (Node 6): Direct Playback 1/2 (Bundle 1) active
    state.routers[NodeId{6}] = RouterState{
        .activeBundles = {RouteBundleId{1}},
    };

    // Meters initialize to minimum (-96 dB)
    state.meters[MeterId{1}] = -96.0;
    state.meters[MeterId{2}] = -96.0;
    state.meters[MeterId{3}] = -96.0;
    state.meters[MeterId{4}] = -96.0;
    state.meters[MeterId{5}] = -96.0;
    state.meters[MeterId{6}] = -96.0;

    return state;
}

} // namespace ASFW::Devices::Duet
