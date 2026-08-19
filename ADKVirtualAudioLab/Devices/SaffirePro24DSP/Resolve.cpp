#include "Resolve.hpp"
#include "Capabilities.hpp"

#include <algorithm>

namespace ASFW::Devices::SaffirePro24DSP {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

std::expected<ResolvedAudioConfiguration, ResolveError> resolve(
    const DeviceConfiguration& config) {

    const auto& caps = capabilities();
    if (std::find(caps.sampleRates.begin(), caps.sampleRates.end(), config.sampleRate) == caps.sampleRates.end()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedSampleRate,
            "Sample rate " + std::to_string(config.sampleRate) + " is not supported by Saffire Pro 24 DSP"
        });
    }

    const OpticalMode optIn = config.opticalInput.value_or(OpticalMode::Adat);
    const OpticalMode optOut = config.opticalOutput.value_or(OpticalMode::Adat);

    ResolvedAudioConfiguration resolved;

    // 1. Streams: 16 capture channels, 8 DAW playback channels
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{StreamDirection::Capture, 16, "Saffire Capture (16 ch)"},
            ResolvedAudioStream{StreamDirection::Playback, 8, "DAW Playback (8 ch)"},
        },
    };

    // 2. Topology
    Topology& t = resolved.topology;
    t.revision = 0;

    const NodeId nPhysIn{1};
    const NodeId nHostIO{2};
    const NodeId nDiceRouter{3};
    const NodeId nMixer{4};
    const NodeId nOutGroup{5};
    const NodeId nChannelStrip{6};
    const NodeId nReverb{7};
    const NodeId nPhysOut{8};

    // Router Ports & Legal Crossbar (46 inputs x 46 outputs = 2116 bundles)
    std::vector<PortId> routerInputs;
    for (uint32_t i = 1; i <= 46; ++i) routerInputs.push_back(PortId{50 + i});

    std::vector<PortId> routerOutputs;
    for (uint32_t i = 1; i <= 46; ++i) routerOutputs.push_back(PortId{100 + i});

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

    // 18x16 Mixer: 18 inputs -> 16 outputs = 288 crosspoints
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

    // Output Group (6 analog router outs -> 10 physical outputs: 6 line + 4 HP)
    std::vector<PortId> outGroupIn;
    for (uint32_t i = 1; i <= 6; ++i) outGroupIn.push_back(PortId{190 + i});
    std::vector<PortId> outGroupOut;
    for (uint32_t i = 1; i <= 10; ++i) outGroupOut.push_back(PortId{200 + i});

    t.nodes = {
        Node{
            nPhysIn,
            optIn == OpticalMode::Adat ? "Physical Inputs (6 Analog + 2 SPDIF + 8 ADAT)" : "Physical Inputs (6 Analog + 2 SPDIF + 2 Opt SPDIF)",
            EndpointNode{EndpointKind::Physical}
        },
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
        Node{
            nPhysOut,
            optOut == OpticalMode::Adat ? "Physical Outputs (6 Analog, 4 Headphone, 2 SPDIF, 8 ADAT)" : "Physical Outputs (6 Analog, 4 Headphone, 2 SPDIF, 2 Opt SPDIF)",
            EndpointNode{EndpointKind::Physical}
        },
    };

    // Ports
    // Physical Inputs
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{i}, nPhysIn, PortDirection::Output, 1, "Phys In: Analog " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{7}, nPhysIn, PortDirection::Output, 1, "Phys In: SPDIF L"});
    t.ports.push_back(Port{PortId{8}, nPhysIn, PortDirection::Output, 1, "Phys In: SPDIF R"});
    const uint32_t optInCount = (optIn == OpticalMode::Adat) ? 8 : 2;
    for (uint32_t i = 1; i <= optInCount; ++i) {
        t.ports.push_back(Port{
            PortId{8 + i},
            nPhysIn,
            PortDirection::Output,
            1,
            (optIn == OpticalMode::Adat) ? ("Phys In: ADAT " + std::to_string(i)) : ("Phys In: Opt SPDIF " + std::to_string(i))
        });
    }

    // Host IO
    for (uint32_t i = 1; i <= 16; ++i) {
        t.ports.push_back(Port{PortId{20 + i}, nHostIO, PortDirection::Input, 1, "Host Stream Capture " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 8; ++i) {
        t.ports.push_back(Port{PortId{40 + i}, nHostIO, PortDirection::Output, 1, "DAW Stream Playback " + std::to_string(i)});
    }

    // Router Ports
    for (uint32_t i = 1; i <= 46; ++i) {
        t.ports.push_back(Port{PortId{50 + i}, nDiceRouter, PortDirection::Input, 1, "Router In " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 46; ++i) {
        t.ports.push_back(Port{PortId{100 + i}, nDiceRouter, PortDirection::Output, 1, "Router Out " + std::to_string(i)});
    }

    // Mixer Ports
    for (uint32_t i = 1; i <= 18; ++i) {
        t.ports.push_back(Port{PortId{150 + i}, nMixer, PortDirection::Input, 1, "Mixer In " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 16; ++i) {
        t.ports.push_back(Port{PortId{170 + i}, nMixer, PortDirection::Output, 1, "Mixer Out " + std::to_string(i)});
    }

    // Output Group Ports
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{190 + i}, nOutGroup, PortDirection::Input, 1, "OutGroup In " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{200 + i}, nOutGroup, PortDirection::Output, 1, "OutGroup Phone Out " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{206 + i}, nOutGroup, PortDirection::Output, 1, "OutGroup HP Out " + std::to_string(i)});
    }

    // Channel Strip & Reverb Ports
    t.ports.push_back(Port{PortId{211}, nChannelStrip, PortDirection::Input, 1, "ChStrip In L"});
    t.ports.push_back(Port{PortId{212}, nChannelStrip, PortDirection::Input, 1, "ChStrip In R"});
    t.ports.push_back(Port{PortId{213}, nChannelStrip, PortDirection::Output, 1, "ChStrip Out L"});
    t.ports.push_back(Port{PortId{214}, nChannelStrip, PortDirection::Output, 1, "ChStrip Out R"});

    t.ports.push_back(Port{PortId{221}, nReverb, PortDirection::Input, 1, "Reverb In L"});
    t.ports.push_back(Port{PortId{222}, nReverb, PortDirection::Input, 1, "Reverb In R"});
    t.ports.push_back(Port{PortId{223}, nReverb, PortDirection::Output, 1, "Reverb Out L"});
    t.ports.push_back(Port{PortId{224}, nReverb, PortDirection::Output, 1, "Reverb Out R"});

    // Physical Outputs
    for (uint32_t i = 1; i <= 6; ++i) {
        t.ports.push_back(Port{PortId{230 + i}, nPhysOut, PortDirection::Input, 1, "Phone Out " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{236 + i}, nPhysOut, PortDirection::Input, 1, "HP Out " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{241}, nPhysOut, PortDirection::Input, 1, "SPDIF Out L"});
    t.ports.push_back(Port{PortId{242}, nPhysOut, PortDirection::Input, 1, "SPDIF Out R"});

    // Fixed Links
    for (uint32_t i = 1; i <= (8 + optInCount); ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{50 + i}});
    }
    for (uint32_t i = 1; i <= 8; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{40 + i}, PortId{66 + i}});
    }
    for (uint32_t i = 1; i <= 16; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{170 + i}, PortId{74 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{213}, PortId{91}});
    t.fixedLinks.push_back(FixedLink{PortId{214}, PortId{92}});
    t.fixedLinks.push_back(FixedLink{PortId{223}, PortId{93}});
    t.fixedLinks.push_back(FixedLink{PortId{224}, PortId{94}});

    for (uint32_t i = 1; i <= 6; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{100 + i}, PortId{190 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{107}, PortId{241}});
    t.fixedLinks.push_back(FixedLink{PortId{108}, PortId{242}});

    for (uint32_t i = 1; i <= 16; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{108 + i}, PortId{20 + i}});
    }
    for (uint32_t i = 1; i <= 18; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{124 + i}, PortId{150 + i}});
    }
    t.fixedLinks.push_back(FixedLink{PortId{143}, PortId{211}});
    t.fixedLinks.push_back(FixedLink{PortId{144}, PortId{212}});
    t.fixedLinks.push_back(FixedLink{PortId{145}, PortId{221}});
    t.fixedLinks.push_back(FixedLink{PortId{146}, PortId{222}});

    for (uint32_t i = 1; i <= 6; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{200 + i}, PortId{230 + i}});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.fixedLinks.push_back(FixedLink{PortId{206 + i}, PortId{236 + i}});
    }

    // Parameters
    t.parameters = {
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
    };

    // Meters
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

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    state.parameters[ParameterId{1}] = false; // Dim off
    state.parameters[ParameterId{2}] = false; // Mute off
    state.parameters[ParameterId{3}] = 100.0; // Monitor 1 Vol
    state.parameters[ParameterId{4}] = 100.0; // Monitor 2 Vol
    state.parameters[ParameterId{5}] = int64_t{0}; // Line
    state.parameters[ParameterId{6}] = int64_t{0}; // Line

    // Default 1-to-1 active bundles in DICE router (e.g. DAW Playback 1/2 -> Analog Out 1/2, Phys In 1..16 -> Stream Cap 1..16)
    // Bundle ID for (In X, Out Y) = (X - 1) * 46 + Y
    // In 17 (DAW 1) -> Out 1 (Analog 1): (17-1)*46 + 1 = 737
    // In 18 (DAW 2) -> Out 2 (Analog 2): (18-1)*46 + 2 = 784
    std::vector<RouteBundleId> initialBundles = {
        RouteBundleId{737},
        RouteBundleId{784},
    };
    // Phys In 1..16 (Inputs 1..16) -> Stream Capture 1..16 (Outputs 9..24):
    for (uint32_t i = 1; i <= 16; ++i) {
        uint32_t bId = (i - 1) * 46 + (8 + i);
        initialBundles.push_back(RouteBundleId{bId});
    }

    state.routers[NodeId{3}] = RouterState{
        .node = NodeId{3},
        .activeBundles = std::move(initialBundles),
    };

    state.meters[MeterId{1}] = -96.0;
    state.meters[MeterId{2}] = -96.0;
    state.meters[MeterId{3}] = -96.0;

    return state;
}

} // namespace ASFW::Devices::SaffirePro24DSP
