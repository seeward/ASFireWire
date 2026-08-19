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

    // Output Selector: 5 physical output pairs, each selectable between:
    // 0: DAW Stream Playback (WavePlay)
    // 1: Direct Thru: Analog In 1/2
    // 2: Direct Thru: Analog In 3/4
    // 3: Direct Thru: Analog In 5/6
    // 4: Direct Thru: Analog In 7/8
    // 5: Direct Thru: Digital In (SPDIF)
    // 6: Digital Mixer Master Mix L/R
    std::vector<PortId> selInputs;
    for (uint32_t i = 1; i <= 10; ++i) selInputs.push_back(PortId{80 + i}); // DAW Playback 1..10
    for (uint32_t i = 1; i <= 10; ++i) selInputs.push_back(PortId{130 + i}); // Physical Thru 1..10
    selInputs.push_back(PortId{91}); // Mixer Out L
    selInputs.push_back(PortId{92}); // Mixer Out R

    std::vector<PortId> selOutputs;
    for (uint32_t i = 1; i <= 10; ++i) selOutputs.push_back(PortId{92 + i});

    std::vector<RouteBundle> outSelBundles;
    std::vector<Presentation::RouteBundleGroup> outBundleGroups;
    const std::vector<std::string> outGroupNames = {
        "Analog Out 1/2 Source", "Analog Out 3/4 Source", "Analog Out 5/6 Source", "Analog Out 7/8 Source", "Digital S/PDIF Out Source"
    };

    uint32_t bId = 1;
    for (uint32_t pair = 0; pair < 5; ++pair) {
        std::vector<RouteBundleId> pairBundleIds;
        const PortId destL{93 + pair * 2};
        const PortId destR{94 + pair * 2};

        // Option 0: DAW Playback
        const auto bDaw = RouteBundleId{bId++};
        pairBundleIds.push_back(bDaw);
        outSelBundles.push_back(RouteBundle{
            bDaw,
            {Route{PortId{81 + pair * 2}, destL}, Route{PortId{82 + pair * 2}, destR}},
        });

        // Option 1..4: Direct Analog Inputs
        for (uint32_t inPair = 0; inPair < 4; ++inPair) {
            const auto bThru = RouteBundleId{bId++};
            pairBundleIds.push_back(bThru);
            outSelBundles.push_back(RouteBundle{
                bThru,
                {Route{PortId{131 + inPair * 2}, destL}, Route{PortId{132 + inPair * 2}, destR}},
            });
        }

        // Option 5: Direct Digital In
        const auto bDigThru = RouteBundleId{bId++};
        pairBundleIds.push_back(bDigThru);
        outSelBundles.push_back(RouteBundle{
            bDigThru,
            {Route{PortId{139}, destL}, Route{PortId{140}, destR}},
        });

        // Option 6: Digital Mixer Master Mix
        const auto bMix = RouteBundleId{bId++};
        pairBundleIds.push_back(bMix);
        outSelBundles.push_back(RouteBundle{
            bMix,
            {Route{PortId{91}, destL}, Route{PortId{92}, destR}},
        });

        outBundleGroups.push_back(Presentation::RouteBundleGroup{
            .name = outGroupNames[pair],
            .bundles = std::move(pairBundleIds),
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
                    .maxDestinationsPerInput = 5,
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
    for (uint32_t i = 1; i <= 10; ++i) {
        t.ports.push_back(Port{PortId{130 + i}, nOutSelector, PortDirection::Input, 1, "OutMux In: Thru In " + std::to_string(i)});
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
        t.fixedLinks.push_back(FixedLink{PortId{i}, PortId{130 + i}});
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

    // 3. Audio Semantics: Logical Channels & Buses
    std::vector<std::string> chNames = {
        "Analog In 1", "Analog In 2", "Analog In 3", "Analog In 4",
        "Analog In 5", "Analog In 6", "Analog In 7", "Analog In 8",
        "Digital In L", "Digital In R",
        "WavePlay L", "WavePlay R",
    };

    for (uint32_t i = 1; i <= 8; ++i) {
        t.channels.push_back(Channel{
            .id = ChannelId{i},
            .name = chNames[i - 1],
            .ports = {PortId{i}, PortId{60 + i}},
        });
    }
    t.channels.push_back(Channel{
        .id = ChannelId{9},
        .name = "Digital In L",
        .ports = {PortId{9}, PortId{69}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{10},
        .name = "Digital In R",
        .ports = {PortId{10}, PortId{70}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{11},
        .name = "WavePlay L",
        .ports = {PortId{51}, PortId{71}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{12},
        .name = "WavePlay R",
        .ports = {PortId{52}, PortId{72}},
    });

    t.buses = {
        Bus{
            .id = BusId{1},
            .semantic = BusSemantic::Main,
            .name = "Digital Mixer Master L/R",
            .ports = {PortId{75}, PortId{76}},
        },
        Bus{
            .id = BusId{2},
            .semantic = BusSemantic::Monitor,
            .name = "Analog Out 1/2",
            .ports = {PortId{93}, PortId{94}},
        },
        Bus{
            .id = BusId{3},
            .semantic = BusSemantic::Aux,
            .name = "Analog Out 3/4",
            .ports = {PortId{95}, PortId{96}},
        },
        Bus{
            .id = BusId{4},
            .semantic = BusSemantic::Aux,
            .name = "Analog Out 5/6",
            .ports = {PortId{97}, PortId{98}},
        },
        Bus{
            .id = BusId{5},
            .semantic = BusSemantic::Aux,
            .name = "Analog Out 7/8",
            .ports = {PortId{99}, PortId{100}},
        },
        Bus{
            .id = BusId{6},
            .semantic = BusSemantic::Aux,
            .name = "Digital S/PDIF Out",
            .ports = {PortId{101}, PortId{102}},
        },
    };

    // Parameters
    uint32_t pId = 1;
    std::vector<ParameterId> panParamIds;

    // 1. Channel Strip Controls (12 Channels)
    for (uint32_t i = 1; i <= 12; ++i) {
        // Send Level to Mixer Master (Crosspoint parameter)
        const uint32_t cpLeft = (i - 1) * 2 + 1;
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = CrosspointId{cpLeft},
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Mixer Send",
        });

        // Pan
        const auto panPid = ParameterId{pId++};
        panParamIds.push_back(panPid);
        t.parameters.push_back(Parameter{
            .id = panPid,
            .target = PortId{60 + i},
            .semantic = ParameterSemantic::Pan,
            .domain = ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            .name = chNames[i - 1] + " Pan",
        });

        // Mute
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = PortId{60 + i},
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = chNames[i - 1] + " Mute",
        });
    }

    // 2. Output Master Controls (5 Physical Pairs + Digital Mixer Sum)
    const auto pMasterVol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMasterVol,
        .target = PortId{75},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Master Mix Volume",
    });

    const auto pMasterMute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMasterMute,
        .target = PortId{75},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Master Mix Mute",
    });

    const std::vector<std::string> outMasterNames = {
        "Analog Out 1/2", "Analog Out 3/4", "Analog Out 5/6", "Analog Out 7/8", "Digital S/PDIF Out"
    };

    for (uint32_t pair = 0; pair < 5; ++pair) {
        const PortId pOutL{93 + pair * 2};
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = pOutL,
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = outMasterNames[pair] + " Master Volume",
        });

        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = pOutL,
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = outMasterNames[pair] + " Master Mute",
        });
    }

    const auto pClock = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pClock,
        .target = nPhysIn,
        .semantic = ParameterSemantic::ClockSource,
        .domain = EnumDomain{
            .values = {
                EnumItem{0, "Internal (32k/44.1k/48k/88.2k/96k)"},
                EnumItem{1, "S/PDIF Coaxial"},
                EnumItem{2, "Word Clock BNC"},
            },
        },
        .name = "Clock Source",
    });

    // Meters
    uint32_t mId = 1;
    for (uint32_t i = 1; i <= 12; ++i) {
        t.meters.push_back(Meter{
            .id = MeterId{mId++},
            .target = PortId{60 + i},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Peak Meter",
        });
    }
    t.meters.push_back(Meter{
        .id = MeterId{mId++},
        .target = PortId{75},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Master Mix Peak Meter L",
    });
    t.meters.push_back(Meter{
        .id = MeterId{mId++},
        .target = PortId{76},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Master Mix Peak Meter R",
    });

    for (uint32_t pair = 0; pair < 5; ++pair) {
        t.meters.push_back(Meter{
            .id = MeterId{mId++},
            .target = PortId{93 + pair * 2},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = outMasterNames[pair] + " Peak Meter L",
        });
        t.meters.push_back(Meter{
            .id = MeterId{mId++},
            .target = PortId{94 + pair * 2},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = outMasterNames[pair] + " Peak Meter R",
        });
    }

    // Presentation Metadata
    resolved.presentation = Presentation::DevicePresentation{
        .groups = {
            Presentation::PresentationGroup{
                .id = Presentation::PresentationGroupId{1},
                .name = "Clock & Sync",
                .kind = Presentation::PresentationGroupKind::Other,
                .parameters = {pClock},
            },
        },
        .routers = {
            Presentation::RouterPresentationHint{
                .router = NodeId{3},
                .style = Presentation::RouterPresentationStyle::Selector,
                .bundleGroups = {
                    Presentation::RouteBundleGroup{
                        .name = "Mixer WavePlay Source",
                        .bundles = {RouteBundleId{1}, RouteBundleId{2}, RouteBundleId{3}, RouteBundleId{4}, RouteBundleId{5}},
                    },
                },
            },
            Presentation::RouterPresentationHint{
                .router = NodeId{5},
                .style = Presentation::RouterPresentationStyle::Selector,
                .bundleGroups = std::move(outBundleGroups),
            },
        },
        .mixers = {
            Presentation::MixerPresentationHint{
                .mixer = NodeId{4},
                .style = Presentation::MixerPresentationStyle::ChannelStrips,
            },
        },
    };

    for (const auto& pid : panParamIds) {
        resolved.presentation.parameters.push_back(Presentation::ParameterPresentationHint{
            .parameter = pid,
            .presentation = Presentation::ControlPresentation::Rotary,
        });
    }

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    state.topologyRevision = resolved.topology.revision;

    for (const auto& param : resolved.topology.parameters) {
        if (std::holds_alternative<BooleanDomain>(param.domain)) {
            state.parameters[param.id] = false;
        } else if (std::holds_alternative<EnumDomain>(param.domain)) {
            state.parameters[param.id] = int64_t{0};
        } else if (auto* sc = std::get_if<ScalarDomain>(&param.domain)) {
            if (param.semantic == ParameterSemantic::Pan) {
                // Hard Left for odd digital/stream channels, hard right for even
                if (param.name == "Digital In L Pan" || param.name == "WavePlay L Pan") {
                    state.parameters[param.id] = -100.0;
                } else if (param.name == "Digital In R Pan" || param.name == "WavePlay R Pan") {
                    state.parameters[param.id] = 100.0;
                } else {
                    state.parameters[param.id] = 0.0;
                }
            } else if (param.semantic == ParameterSemantic::Level) {
                state.parameters[param.id] = 0.0; // Default faders to 0 dB
            } else {
                state.parameters[param.id] = sc->min;
            }
        }
    }

    // Pre-mixer Stream Source Selector (Node 3): Playback 1/2 (Bundle 1)
    state.routers[NodeId{3}] = RouterState{
        .activeBundles = {RouteBundleId{1}},
    };

    // Output Selector (Node 5): 5 direct stream playback bundles (Bundles 1, 8, 15, 22, 29)
    state.routers[NodeId{5}] = RouterState{
        .activeBundles = {RouteBundleId{1}, RouteBundleId{8}, RouteBundleId{15}, RouteBundleId{22}, RouteBundleId{29}},
    };

    for (const auto& meter : resolved.topology.meters) {
        state.meters[meter.id] = -96.0;
    }

    return state;
}

} // namespace ASFW::Devices::Phase88
