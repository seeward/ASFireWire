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

    if (!config.opticalInput.has_value() || !config.opticalOutput.has_value()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::InvalidConfiguration,
            "Saffire requires explicit optical input and output modes in configuration"
        });
    }

    const OpticalMode optIn = *config.opticalInput;
    const OpticalMode optOut = *config.opticalOutput;

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

    // Router Ports with Semantic Hardware Source/Destination Names
    for (uint32_t i = 1; i <= 46; ++i) {
        std::string srcName;
        if (i <= 6) srcName = "Analog In " + std::to_string(i);
        else if (i <= 8) srcName = (i == 7 ? "SPDIF In L" : "SPDIF In R");
        else if (i <= 16) srcName = (optIn == OpticalMode::Adat ? "ADAT In " + std::to_string(i - 8) : "Opt SPDIF In " + std::to_string(i - 8));
        else if (i <= 24) srcName = "DAW Playback " + std::to_string(i - 16);
        else if (i <= 40) srcName = "Mixer Out " + std::to_string(i - 24);
        else if (i <= 42) srcName = (i == 41 ? "ChStrip Out L" : "ChStrip Out R");
        else if (i <= 44) srcName = (i == 43 ? "Reverb Out L" : "Reverb Out R");
        else srcName = "Aux In " + std::to_string(i - 44);

        t.ports.push_back(Port{PortId{50 + i}, nDiceRouter, PortDirection::Input, 1, "Router In: " + srcName});
    }

    for (uint32_t i = 1; i <= 46; ++i) {
        std::string dstName;
        if (i <= 6) dstName = "Line/Monitor " + std::to_string(i);
        else if (i <= 8) dstName = (i == 7 ? "Coax SPDIF L" : "Coax SPDIF R");
        else if (i <= 24) dstName = "DAW Record " + std::to_string(i - 8);
        else if (i <= 42) dstName = "Mixer In " + std::to_string(i - 24);
        else if (i <= 44) dstName = (i == 43 ? "ChStrip In L" : "ChStrip In R");
        else dstName = (i == 45 ? "Reverb In L" : "Reverb In R");

        t.ports.push_back(Port{PortId{100 + i}, nDiceRouter, PortDirection::Output, 1, "Router Out: " + dstName});
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
        t.ports.push_back(Port{PortId{230 + i}, nPhysOut, PortDirection::Input, 1, "Phys Out: Phone " + std::to_string(i)});
    }
    for (uint32_t i = 1; i <= 4; ++i) {
        t.ports.push_back(Port{PortId{236 + i}, nPhysOut, PortDirection::Input, 1, "Phys Out: HP " + std::to_string(i)});
    }
    t.ports.push_back(Port{PortId{241}, nPhysOut, PortDirection::Input, 1, "Phys Out: Coax SPDIF L"});
    t.ports.push_back(Port{PortId{242}, nPhysOut, PortDirection::Input, 1, "Phys Out: Coax SPDIF R"});

    const uint32_t optOutCount = (optOut == OpticalMode::Adat) ? 8 : 2;
    for (uint32_t i = 1; i <= optOutCount; ++i) {
        t.ports.push_back(Port{
            PortId{242 + i},
            nPhysOut,
            PortDirection::Input,
            1,
            (optOut == OpticalMode::Adat) ? ("Phys Out: ADAT " + std::to_string(i)) : ("Phys Out: Opt SPDIF " + std::to_string(i))
        });
    }

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

    // Optical Output Destination Wiring:
    // In optical S/PDIF mode: Optical S/PDIF TX mirrors DICE Router Out 7/8 (Coax S/PDIF TX).
    // In ADAT mode: ADAT Out 1..8 physical destinations are fed from Host Playback streams 1..8.
    // NOTE: Verify against native DICE II hardware register routing map when hardware registers
    // are mapped to determine if ADAT TX channels are exposed as discrete router output ports.
    if (optOut == OpticalMode::Adat) {
        for (uint32_t i = 1; i <= 8; ++i) {
            t.fixedLinks.push_back(FixedLink{PortId{40 + i}, PortId{242 + i}});
        }
    } else {
        t.fixedLinks.push_back(FixedLink{PortId{107}, PortId{243}});
        t.fixedLinks.push_back(FixedLink{PortId{108}, PortId{244}});
    }

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

    // 3. Audio Semantics: Logical Channels & Buses
    std::vector<std::string> chNames = {
        "Anlg In 1", "Anlg In 2", "Anlg In 3", "Anlg In 4",
        "SPDIF In 1", "SPDIF In 2",
        "Loopback 1", "Loopback 2",
        "DAW Playback 1", "DAW Playback 2", "DAW Playback 3", "DAW Playback 4",
    };

    for (uint32_t i = 1; i <= 4; ++i) {
        t.channels.push_back(Channel{
            .id = ChannelId{i},
            .name = chNames[i - 1],
            .ports = {PortId{i}, PortId{50 + i}, PortId{150 + i}},
        });
    }
    t.channels.push_back(Channel{
        .id = ChannelId{5},
        .name = "SPDIF In 1",
        .ports = {PortId{7}, PortId{57}, PortId{155}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{6},
        .name = "SPDIF In 2",
        .ports = {PortId{8}, PortId{58}, PortId{156}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{7},
        .name = "Loopback 1",
        .ports = {PortId{5}, PortId{55}, PortId{157}},
    });
    t.channels.push_back(Channel{
        .id = ChannelId{8},
        .name = "Loopback 2",
        .ports = {PortId{6}, PortId{56}, PortId{158}},
    });
    for (uint32_t i = 1; i <= 4; ++i) {
        t.channels.push_back(Channel{
            .id = ChannelId{8 + i},
            .name = "DAW Playback " + std::to_string(i),
            .ports = {PortId{40 + i}, PortId{66 + i}, PortId{158 + i}},
        });
    }

    t.buses = {
        Bus{
            .id = BusId{1},
            .semantic = BusSemantic::Main,
            .name = "Mix 1/2",
            .ports = {PortId{171}, PortId{172}},
        },
        Bus{
            .id = BusId{2},
            .semantic = BusSemantic::Aux,
            .name = "Mix 3/4",
            .ports = {PortId{173}, PortId{174}},
        },
        Bus{
            .id = BusId{3},
            .semantic = BusSemantic::Monitor,
            .name = "Monitor 1/2",
            .ports = {PortId{201}, PortId{202}},
        },
        Bus{
            .id = BusId{4},
            .semantic = BusSemantic::Aux,
            .name = "Line 3/4",
            .ports = {PortId{203}, PortId{204}},
        },
        Bus{
            .id = BusId{5},
            .semantic = BusSemantic::Cue,
            .name = "Headphone 1",
            .ports = {PortId{207}, PortId{208}},
        },
        Bus{
            .id = BusId{6},
            .semantic = BusSemantic::Cue,
            .name = "Headphone 2",
            .ports = {PortId{209}, PortId{210}},
        },
    };

    // Parameters
    uint32_t pId = 1;

    // 1. Preamp Controls
    const auto pMode1 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMode1,
        .target = PortId{1},
        .semantic = ParameterSemantic::NominalLevel,
        .domain = EnumDomain{{
            {0, "Line"},
            {1, "Instrument"},
        }},
        .name = "Anlg In 1 Mode",
    });

    const auto pPhantom1 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pPhantom1,
        .target = PortId{1},
        .semantic = ParameterSemantic::PhantomPower,
        .domain = BooleanDomain{},
        .name = "Anlg In 1 +48V",
    });

    const auto pMode2 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMode2,
        .target = PortId{2},
        .semantic = ParameterSemantic::NominalLevel,
        .domain = EnumDomain{{
            {0, "Line"},
            {1, "Instrument"},
        }},
        .name = "Anlg In 2 Mode",
    });

    const auto pPhantom2 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pPhantom2,
        .target = PortId{2},
        .semantic = ParameterSemantic::PhantomPower,
        .domain = BooleanDomain{},
        .name = "Anlg In 2 +48V",
    });

    const auto pNominal3 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pNominal3,
        .target = PortId{3},
        .semantic = ParameterSemantic::NominalLevel,
        .domain = EnumDomain{{
            {0, "High Gain (-10 dBV)"},
            {1, "Low Gain (+4 dBu)"},
        }},
        .name = "Anlg In 3 Reference Level",
    });

    const auto pNominal4 = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pNominal4,
        .target = PortId{4},
        .semantic = ParameterSemantic::NominalLevel,
        .domain = EnumDomain{{
            {0, "High Gain (-10 dBV)"},
            {1, "Low Gain (+4 dBu)"},
        }},
        .name = "Anlg In 4 Reference Level",
    });

    // 2. Channel Strip Mixer Sends, Pans, Mutes, Solos (12 Channels)
    std::vector<ParameterId> panParamIds;
    std::vector<ParameterId> auxSendParamIds;

    for (uint32_t i = 1; i <= 12; ++i) {
        // Mix 1/2 Send (Crosspoint to Bus 1)
        const uint32_t cpMain = (i - 1) * 16 + 1;
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = CrosspointId{cpMain},
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -128.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Mix 1/2 Send",
        });

        // Mix 3/4 Send (Crosspoint to Bus 2)
        const uint32_t cpAux = (i - 1) * 16 + 3;
        const auto auxPid = ParameterId{pId++};
        auxSendParamIds.push_back(auxPid);
        t.parameters.push_back(Parameter{
            .id = auxPid,
            .target = CrosspointId{cpAux},
            .semantic = ParameterSemantic::Level,
            .domain = ScalarDomain{.min = -128.0, .max = 6.0, .step = 0.5, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Mix 3/4 Send",
        });

        // Pan
        const auto panPid = ParameterId{pId++};
        panParamIds.push_back(panPid);
        t.parameters.push_back(Parameter{
            .id = panPid,
            .target = PortId{150 + i},
            .semantic = ParameterSemantic::Pan,
            .domain = ScalarDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent},
            .name = chNames[i - 1] + " Pan",
        });

        // Mute
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = PortId{150 + i},
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = chNames[i - 1] + " Mute",
        });

        // Solo
        t.parameters.push_back(Parameter{
            .id = ParameterId{pId++},
            .target = PortId{150 + i},
            .semantic = ParameterSemantic::Solo,
            .domain = BooleanDomain{},
            .name = chNames[i - 1] + " Solo",
        });
    }

    // 3. VRM DSP Engine Parameters (PRO 24 DSP Exclusive)
    const auto pVrmBypass = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pVrmBypass,
        .target = nReverb,
        .semantic = ParameterSemantic::Unknown,
        .domain = BooleanDomain{},
        .name = "VRM Bypass",
    });

    const auto pVrmRoom = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pVrmRoom,
        .target = nReverb,
        .semantic = ParameterSemantic::Unknown,
        .domain = EnumDomain{{
            {0, "Professional Studio (Acoustically Treated)"},
            {1, "Living Room (Furnished)"},
            {2, "Bedroom Studio"},
        }},
        .name = "VRM Room Environment",
    });

    const auto pVrmSpeaker = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pVrmSpeaker,
        .target = nReverb,
        .semantic = ParameterSemantic::Unknown,
        .domain = EnumDomain{{
            {0, "Professional Reference (Nearfield)"},
            {1, "Hi-Fi Stereo (Audiophile)"},
            {2, "Consumer Desktop"},
        }},
        .name = "VRM Speaker Model",
    });

    const auto pVrmPosition = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pVrmPosition,
        .target = nReverb,
        .semantic = ParameterSemantic::Unknown,
        .domain = EnumDomain{{
            {0, "Sweet Spot (Center)"},
            {1, "Left of Sweet Spot"},
            {2, "Right of Sweet Spot"},
        }},
        .name = "VRM Listening Position",
    });

    // 4. Output Master Controls
    const auto pMonitorVol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMonitorVol,
        .target = PortId{201},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Monitor 1/2 Master Volume",
    });

    const auto pMonitorMute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMonitorMute,
        .target = PortId{201},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Monitor 1/2 Master Mute",
    });

    const auto pMonitorDim = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMonitorDim,
        .target = PortId{201},
        .semantic = ParameterSemantic::Dim,
        .domain = BooleanDomain{},
        .name = "Monitor 1/2 Master Dim",
    });

    const auto pLine34Vol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pLine34Vol,
        .target = PortId{203},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Line 3/4 Volume",
    });

    const auto pLine34Mute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pLine34Mute,
        .target = PortId{203},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Line 3/4 Mute",
    });

    const auto pHP1Vol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pHP1Vol,
        .target = PortId{207},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Headphone 1 Volume",
    });

    const auto pHP1Mute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pHP1Mute,
        .target = PortId{207},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Headphone 1 Mute",
    });

    const auto pHP2Vol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pHP2Vol,
        .target = PortId{209},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Headphone 2 Volume",
    });

    const auto pHP2Mute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pHP2Mute,
        .target = PortId{209},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Headphone 2 Mute",
    });

    const auto pMix12Vol = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMix12Vol,
        .target = PortId{171},
        .semantic = ParameterSemantic::Level,
        .domain = ScalarDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels},
        .name = "Mix 1/2 Master Level",
    });

    const auto pMix12Mute = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pMix12Mute,
        .target = PortId{171},
        .semantic = ParameterSemantic::Mute,
        .domain = BooleanDomain{},
        .name = "Mix 1/2 Master Mute",
    });

    const auto pClock = ParameterId{pId++};
    t.parameters.push_back(Parameter{
        .id = pClock,
        .target = nPhysIn,
        .semantic = ParameterSemantic::ClockSource,
        .domain = EnumDomain{
            .values = {
                EnumItem{0, "Internal"},
                EnumItem{1, "S/PDIF Coaxial"},
                EnumItem{2, "ADAT / Optical"},
            },
        },
        .name = "Clock Source",
    });

    // 5. Meters (12 Channel Meters + 5 Output Meters)
    for (uint32_t i = 1; i <= 12; ++i) {
        t.meters.push_back(Meter{
            .id = MeterId{i},
            .target = PortId{150 + i},
            .semantic = MeterSemantic::Peak,
            .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
            .name = chNames[i - 1] + " Peak Meter",
        });
    }

    t.meters.push_back(Meter{
        .id = MeterId{13},
        .target = PortId{171},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Mix 1/2 Peak Meter L",
    });
    t.meters.push_back(Meter{
        .id = MeterId{14},
        .target = PortId{172},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Mix 1/2 Peak Meter R",
    });
    t.meters.push_back(Meter{
        .id = MeterId{15},
        .target = PortId{201},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Monitor 1/2 Peak Meter L",
    });
    t.meters.push_back(Meter{
        .id = MeterId{16},
        .target = PortId{202},
        .semantic = MeterSemantic::Peak,
        .domain = ScalarDomain{.min = -96.0, .max = 0.0, .unit = ScalarUnit::Decibels},
        .name = "Monitor 1/2 Peak Meter R",
    });

    // 6. Presentation Metadata
    resolved.presentation = Presentation::DevicePresentation{
        .groups = {
            Presentation::PresentationGroup{
                .id = Presentation::PresentationGroupId{1},
                .name = "VRM (Virtual Reference Monitoring)",
                .kind = Presentation::PresentationGroupKind::Processor,
                .nodes = {nReverb},
                .parameters = {pVrmBypass, pVrmRoom, pVrmSpeaker, pVrmPosition},
            },
        },
        .routers = {
            Presentation::RouterPresentationHint{
                .router = NodeId{3},
                .style = Presentation::RouterPresentationStyle::Matrix,
                .inputGroups = {
                    Presentation::PortPresentationGroup{
                        .name = "Analog Inputs",
                        .ports = {PortId{51}, PortId{52}, PortId{53}, PortId{54}, PortId{55}, PortId{56}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "S/PDIF Inputs",
                        .ports = {PortId{57}, PortId{58}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "ADAT / Opt In",
                        .ports = {PortId{59}, PortId{60}, PortId{61}, PortId{62}, PortId{63}, PortId{64}, PortId{65}, PortId{66}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "DAW Playback",
                        .ports = {PortId{67}, PortId{68}, PortId{69}, PortId{70}, PortId{71}, PortId{72}, PortId{73}, PortId{74}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "Mixer Outputs",
                        .ports = {PortId{75}, PortId{76}, PortId{77}, PortId{78}, PortId{79}, PortId{80}, PortId{81}, PortId{82}, PortId{83}, PortId{84}, PortId{85}, PortId{86}, PortId{87}, PortId{88}, PortId{89}, PortId{90}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "DSP FX Returns",
                        .ports = {PortId{91}, PortId{92}, PortId{93}, PortId{94}},
                    },
                },
                .outputGroups = {
                    Presentation::PortPresentationGroup{
                        .name = "Monitor & Line Out",
                        .ports = {PortId{101}, PortId{102}, PortId{103}, PortId{104}, PortId{105}, PortId{106}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "Coax S/PDIF Out",
                        .ports = {PortId{107}, PortId{108}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "DAW Record",
                        .ports = {PortId{109}, PortId{110}, PortId{111}, PortId{112}, PortId{113}, PortId{114}, PortId{115}, PortId{116}, PortId{117}, PortId{118}, PortId{119}, PortId{120}, PortId{121}, PortId{122}, PortId{123}, PortId{124}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "Mixer Inputs",
                        .ports = {PortId{125}, PortId{126}, PortId{127}, PortId{128}, PortId{129}, PortId{130}, PortId{131}, PortId{132}, PortId{133}, PortId{134}, PortId{135}, PortId{136}, PortId{137}, PortId{138}, PortId{139}, PortId{140}, PortId{141}, PortId{142}},
                    },
                    Presentation::PortPresentationGroup{
                        .name = "DSP FX Inputs",
                        .ports = {PortId{143}, PortId{144}, PortId{145}, PortId{146}},
                    },
                },
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
    for (const auto& pid : auxSendParamIds) {
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
                state.parameters[param.id] = 0.0;
            } else if (param.semantic == ParameterSemantic::Level) {
                // If it's a main send (Mix 1/2) or master monitor volume, default to 0 dB
                if (param.name.find("Mix 1/2 Send") != std::string::npos || param.name.find("Master") != std::string::npos || param.name.find("Volume") != std::string::npos) {
                    state.parameters[param.id] = 0.0;
                } else {
                    state.parameters[param.id] = sc->min;
                }
            } else {
                state.parameters[param.id] = sc->min;
            }
        }
    }

    // Default 1-to-1 active bundles in DICE router (e.g. DAW Playback 1/2 -> Analog Out 1/2)
    std::vector<RouteBundleId> initialBundles = {
        RouteBundleId{737}, // DAW 1 -> Analog 1
        RouteBundleId{784}, // DAW 2 -> Analog 2
    };

    // Count physical input ports connected to router inputs (1..N)
    uint32_t activePhysIn = 0;
    for (const auto& port : resolved.topology.ports) {
        if (port.owner == NodeId{1} && port.direction == PortDirection::Output) {
            ++activePhysIn;
        }
    }
    // Only activate capture routes for actual connected physical inputs
    for (uint32_t i = 1; i <= activePhysIn && i <= 16; ++i) {
        uint32_t bId = (i - 1) * 46 + (8 + i);
        initialBundles.push_back(RouteBundleId{bId});
    }

    state.routers[NodeId{3}] = RouterState{
        .activeBundles = std::move(initialBundles),
    };

    for (const auto& meter : resolved.topology.meters) {
        state.meters[meter.id] = -96.0;
    }

    return state;
}

} // namespace ASFW::Devices::SaffirePro24DSP
