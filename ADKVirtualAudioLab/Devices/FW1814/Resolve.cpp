#include "Resolve.hpp"
#include "Capabilities.hpp"

#include <algorithm>
#include <string>

namespace ASFW::Devices::FW1814 {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

namespace {

// Geometry, from the two references that agree on it.
//
// Stream channel counts: Linux `bebob_maudio.c:228-241` ch_table, which
// decomposes exactly as `8 analog + digital_in` for capture and
// `4 analog + digital_out` for playback, with ADAT S/MUX halving 8 -> 4 above
// 48 kHz. Corroborated on the device by the firmware's own I18S/I14S bus
// naming (documentation/1814.md 3.2a).
//
// Mixer shape: the ALSA userspace BeBoB crate's parameter window,
// `protocols/bebob/src/maudio/special.rs` -- MaudioSpecialMixerParameters is
// `[[bool; 4]; 2]` analog, `[bool; 2]` S/PDIF, `[[bool; 4]; 2]` ADAT and
// `[[bool; 2]; 2]` stream pairs, i.e. two stereo mixer outputs fed from eleven
// stereo input pairs, plus a separate aux mixer over the same inputs.
struct Geometry {
    uint32_t digitalInPairs{};
    uint32_t digitalOutPairs{};
    uint32_t mixerPairs{};
    uint32_t capturePcm{};
    uint32_t playbackPcm{};
    SignalKind digitalInKind{};
    SignalKind digitalOutKind{};
    // The digital input is one selected source. In S/PDIF format the device
    // offers two connectors for it, so the choice becomes a router.
    bool hasDigitalInputSelector{};
};

constexpr uint32_t kAnalogInPairs = 4;   // 8 analog inputs
constexpr uint32_t kAnalogOutPairs = 2;  // 4 analog outputs
constexpr uint32_t kStreamMixerPairs = 2;
constexpr uint32_t kHeadphonePairs = 2;

Geometry geometryFor(const DeviceConfiguration& config) {
    // ADAT carries 8 channels at 44.1/48 kHz and 4 under S/MUX above that;
    // either S/PDIF variant carries one pair at every rate.
    //
    // OPEN: derived from rate here because Linux's formation table is indexed
    // purely by rate band, and that table *is* the stream geometry the driver
    // negotiates. The M-Audio Panel also carries an independent S/MUX checkbox
    // (control 5531 -> setting 1553, enabled only when either optical setting
    // is ADAT) that hides the last two ADAT pairs, so the two sources disagree
    // on whether S/MUX is settable at 44.1/48 kHz. Needs hardware to settle.
    const bool smux = config.sampleRate > 48000;
    const auto optIn = config.opticalInput.value_or(OpticalMode::Adat);
    const auto optOut = config.opticalOutput.value_or(OpticalMode::Adat);

    Geometry geometry;
    geometry.digitalInPairs = (optIn == OpticalMode::Adat) ? (smux ? 2 : 4) : 1;
    geometry.digitalOutPairs = (optOut == OpticalMode::Adat) ? (smux ? 2 : 4) : 1;
    geometry.digitalInKind = (optIn == OpticalMode::Adat) ? SignalKind::Adat : SignalKind::SpdifOptical;
    geometry.digitalOutKind = (optOut == OpticalMode::Adat) ? SignalKind::Adat : SignalKind::SpdifOptical;
    geometry.hasDigitalInputSelector = (optIn != OpticalMode::Adat);
    geometry.mixerPairs = kAnalogInPairs + geometry.digitalInPairs + kStreamMixerPairs;
    geometry.capturePcm = (kAnalogInPairs + geometry.digitalInPairs) * 2;
    geometry.playbackPcm = (kAnalogOutPairs + geometry.digitalOutPairs) * 2;
    return geometry;
}

// Port identifiers. Ranges are spaced so a mode change never reuses an id for a
// different signal within one revision.
constexpr uint32_t kPhysAnalogIn = 1;    // .. 4
constexpr uint32_t kPhysSpdifCoaxIn = 8; // S/PDIF format only
constexpr uint32_t kPhysDigitalIn = 11;  // .. 14 (the optical connector)
constexpr uint32_t kHostCapture = 21;    // .. 28
constexpr uint32_t kHostPlayback = 41;   // .. 46
constexpr uint32_t kMainMixerIn = 61;    // .. 71
constexpr uint32_t kMainMixerOut0 = 81;
constexpr uint32_t kMainMixerOut1 = 82;
constexpr uint32_t kAuxMixerIn = 91;     // .. 101
constexpr uint32_t kAuxMixerOut = 111;
constexpr uint32_t kAnalogMuxMix0 = 121;
constexpr uint32_t kAnalogMuxMix1 = 122;
constexpr uint32_t kAnalogMuxAux = 123;
constexpr uint32_t kAnalogMuxOut = 126;  // .. 127
constexpr uint32_t kPhoneMuxMix0 = 131;
constexpr uint32_t kPhoneMuxMix1 = 132;
constexpr uint32_t kPhoneMuxAux = 133;
constexpr uint32_t kPhoneMuxOut = 136;   // .. 137
constexpr uint32_t kPhysAnalogOut = 141; // .. 142
constexpr uint32_t kPhysHeadphone = 143; // .. 144
constexpr uint32_t kPhysDigitalOut = 151;// .. 154
constexpr uint32_t kDigitalMuxCoax = 161;
constexpr uint32_t kDigitalMuxOptical = 162;
constexpr uint32_t kDigitalMuxOut = 165;

const NodeId nPhysIn{1};
const NodeId nHostIO{2};
const NodeId nMainMixer{3};
const NodeId nAuxMixer{4};
const NodeId nAnalogOutMux{5};
const NodeId nHeadphoneMux{6};
const NodeId nPhysOut{7};
const NodeId nDigitalInMux{8};

// dB range of every gain and volume in the parameter window
// (documentation/1814.md 6.4: "Range -128...0 dB").
constexpr ScalarDomain kGainDomain{.min = -128.0, .max = 0.0, .step = 0.5, .unit = ScalarUnit::Decibels};
constexpr ScalarDomain kBalanceDomain{.min = -100.0, .max = 100.0, .step = 1.0, .unit = ScalarUnit::Percent};
constexpr ScalarDomain kMeterDomain{.min = -128.0, .max = 0.0, .unit = ScalarUnit::Decibels};

std::string mixerInputName(const Geometry& geometry, uint32_t index) {
    if (index < kAnalogInPairs) {
        return "Line In " + std::to_string(index * 2 + 1) + "/" + std::to_string(index * 2 + 2);
    }
    if (index < kAnalogInPairs + geometry.digitalInPairs) {
        const uint32_t digital = index - kAnalogInPairs;
        const char* label = (geometry.digitalInKind == SignalKind::Adat) ? "ADAT In " : "Opt S/PDIF In ";
        return label + std::to_string(digital * 2 + 1) + "/" + std::to_string(digital * 2 + 2);
    }
    const uint32_t stream = index - kAnalogInPairs - geometry.digitalInPairs;
    return "Playback " + std::to_string(stream * 2 + 1) + "/" + std::to_string(stream * 2 + 2);
}

// The port carrying digital input pair `i` into the rest of the graph: the
// connector itself for ADAT, or the selector's output when the format is
// S/PDIF and the user picks between the coaxial and optical jacks.
PortId digitalInputSource(const Geometry& geometry, uint32_t i) {
    return geometry.hasDigitalInputSelector ? PortId{kDigitalMuxOut} : PortId{kPhysDigitalIn + i};
}

bool mixerInputIsPhysical(const Geometry& geometry, uint32_t index) {
    return index < kAnalogInPairs + geometry.digitalInPairs;
}

} // namespace

std::expected<ResolvedAudioConfiguration, ResolveError> resolve(
    const DeviceConfiguration& config) {

    const auto& caps = capabilities();
    if (std::find(caps.sampleRates.begin(), caps.sampleRates.end(), config.sampleRate) == caps.sampleRates.end()) {
        return std::unexpected(ResolveError{
            ResolveErrorKind::UnsupportedSampleRate,
            "Sample rate " + std::to_string(config.sampleRate) + " is not supported by FW1814"
        });
    }

    const Geometry geometry = geometryFor(config);

    ResolvedAudioConfiguration resolved;

    // 1. Streams
    resolved.streams = ResolvedStreamConfiguration{
        .sampleRate = config.sampleRate,
        .streams = {
            ResolvedAudioStream{
                StreamDirection::Capture,
                geometry.capturePcm,
                "FW1814 Capture (" + std::to_string(geometry.capturePcm) + " ch)",
            },
            ResolvedAudioStream{
                StreamDirection::Playback,
                geometry.playbackPcm,
                "FW1814 Playback (" + std::to_string(geometry.playbackPcm) + " ch)",
            },
        },
    };

    // 2. Topology
    Topology& t = resolved.topology;
    t.revision = 0;

    const uint32_t capturePairs = kAnalogInPairs + geometry.digitalInPairs;
    const uint32_t playbackPairs = kStreamMixerPairs + geometry.digitalOutPairs;

    // --- Ports -----------------------------------------------------------

    for (uint32_t i = 0; i < kAnalogInPairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kPhysAnalogIn + i}, nPhysIn, PortDirection::Output, 2,
                                       {SignalKind::AnalogLine, i * 2 + 1}));
    }
    for (uint32_t i = 0; i < geometry.digitalInPairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kPhysDigitalIn + i}, nPhysIn, PortDirection::Output, 2,
                                       {geometry.digitalInKind, i * 2 + 1}));
    }
    if (geometry.hasDigitalInputSelector) {
        t.ports.push_back(endpointPort(PortId{kPhysSpdifCoaxIn}, nPhysIn, PortDirection::Output, 2,
                                       {SignalKind::SpdifCoaxial, 1}));
        t.ports.push_back(Port{PortId{kDigitalMuxCoax}, nDigitalInMux, PortDirection::Input, 2, "Coaxial"});
        t.ports.push_back(Port{PortId{kDigitalMuxOptical}, nDigitalInMux, PortDirection::Input, 2, "Optical"});
        t.ports.push_back(Port{PortId{kDigitalMuxOut}, nDigitalInMux, PortDirection::Output, 2, "Digital In"});
    }

    for (uint32_t i = 0; i < capturePairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kHostCapture + i}, nHostIO, PortDirection::Input, 2,
                                       {SignalKind::HostStream, i * 2 + 1}));
    }
    for (uint32_t i = 0; i < playbackPairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kHostPlayback + i}, nHostIO, PortDirection::Output, 2,
                                       {SignalKind::HostStream, i * 2 + 1}));
    }

    std::vector<PortId> mainInputs;
    std::vector<PortId> auxInputs;
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        const std::string name = mixerInputName(geometry, i);
        mainInputs.push_back(PortId{kMainMixerIn + i});
        auxInputs.push_back(PortId{kAuxMixerIn + i});
        t.ports.push_back(Port{PortId{kMainMixerIn + i}, nMainMixer, PortDirection::Input, 2, "Mixer In: " + name});
        t.ports.push_back(Port{PortId{kAuxMixerIn + i}, nAuxMixer, PortDirection::Input, 2, "Aux In: " + name});
    }
    t.ports.push_back(Port{PortId{kMainMixerOut0}, nMainMixer, PortDirection::Output, 2, "Mixer 1 Out"});
    t.ports.push_back(Port{PortId{kMainMixerOut1}, nMainMixer, PortDirection::Output, 2, "Mixer 2 Out"});
    t.ports.push_back(Port{PortId{kAuxMixerOut}, nAuxMixer, PortDirection::Output, 2, "Aux Out"});

    t.ports.push_back(Port{PortId{kAnalogMuxMix0}, nAnalogOutMux, PortDirection::Input, 2, "Mixer 1"});
    t.ports.push_back(Port{PortId{kAnalogMuxMix1}, nAnalogOutMux, PortDirection::Input, 2, "Mixer 2"});
    t.ports.push_back(Port{PortId{kAnalogMuxAux}, nAnalogOutMux, PortDirection::Input, 2, "Aux"});
    for (uint32_t i = 0; i < kAnalogOutPairs; ++i) {
        t.ports.push_back(Port{PortId{kAnalogMuxOut + i}, nAnalogOutMux, PortDirection::Output, 2,
                               "Analog Out Pair " + std::to_string(i + 1)});
    }

    t.ports.push_back(Port{PortId{kPhoneMuxMix0}, nHeadphoneMux, PortDirection::Input, 2, "Mixer 1"});
    t.ports.push_back(Port{PortId{kPhoneMuxMix1}, nHeadphoneMux, PortDirection::Input, 2, "Mixer 2"});
    t.ports.push_back(Port{PortId{kPhoneMuxAux}, nHeadphoneMux, PortDirection::Input, 2, "Aux"});
    for (uint32_t i = 0; i < kHeadphonePairs; ++i) {
        t.ports.push_back(Port{PortId{kPhoneMuxOut + i}, nHeadphoneMux, PortDirection::Output, 2,
                               "Headphone Pair " + std::to_string(i + 1)});
    }

    for (uint32_t i = 0; i < kAnalogOutPairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kPhysAnalogOut + i}, nPhysOut, PortDirection::Input, 2,
                                       {SignalKind::AnalogLine, i * 2 + 1}));
    }
    for (uint32_t i = 0; i < kHeadphonePairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kPhysHeadphone + i}, nPhysOut, PortDirection::Input, 2,
                                       {SignalKind::Headphone, i * 2 + 1}));
    }
    for (uint32_t i = 0; i < geometry.digitalOutPairs; ++i) {
        t.ports.push_back(endpointPort(PortId{kPhysDigitalOut + i}, nPhysOut, PortDirection::Input, 2,
                                       {geometry.digitalOutKind, i * 2 + 1}));
    }

    // --- Nodes -----------------------------------------------------------

    std::vector<MixerCrosspoint> mainCrosspoints;
    uint32_t crosspointId = 1;
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        mainCrosspoints.push_back({CrosspointId{crosspointId++}, PortId{kMainMixerIn + i}, PortId{kMainMixerOut0}});
    }
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        mainCrosspoints.push_back({CrosspointId{crosspointId++}, PortId{kMainMixerIn + i}, PortId{kMainMixerOut1}});
    }
    std::vector<MixerCrosspoint> auxCrosspoints;
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        auxCrosspoints.push_back({CrosspointId{crosspointId++}, PortId{kAuxMixerIn + i}, PortId{kAuxMixerOut}});
    }

    std::vector<RouteBundle> analogBundles;
    for (uint32_t pair = 0; pair < kAnalogOutPairs; ++pair) {
        const PortId out{kAnalogMuxOut + pair};
        analogBundles.push_back({RouteBundleId{pair * 2 + 1}, {Route{PortId{kAnalogMuxMix0 + pair}, out}}});
        analogBundles.push_back({RouteBundleId{pair * 2 + 2}, {Route{PortId{kAnalogMuxAux}, out}}});
    }

    std::vector<RouteBundle> phoneBundles;
    uint32_t phoneBundleId = 1;
    for (uint32_t pair = 0; pair < kHeadphonePairs; ++pair) {
        const PortId out{kPhoneMuxOut + pair};
        for (uint32_t source = 0; source < 3; ++source) {
            phoneBundles.push_back({RouteBundleId{phoneBundleId++}, {Route{PortId{kPhoneMuxMix0 + source}, out}}});
        }
    }

    t.nodes = {
        Node{nPhysIn, "Physical Inputs", EndpointNode{EndpointKind::Physical}},
        Node{nHostIO, "Host Audio Streams", EndpointNode{EndpointKind::Host}},
        Node{
            nMainMixer,
            "Main Mixer",
            MixerNode{
                .inputs = mainInputs,
                .outputs = {PortId{kMainMixerOut0}, PortId{kMainMixerOut1}},
                .crosspoints = std::move(mainCrosspoints),
            },
        },
        Node{
            nAuxMixer,
            "Aux Mixer",
            MixerNode{
                .inputs = auxInputs,
                .outputs = {PortId{kAuxMixerOut}},
                .crosspoints = std::move(auxCrosspoints),
            },
        },
        Node{
            nAnalogOutMux,
            "Analog Output Pair Source",
            RouterNode{
                .inputs = {PortId{kAnalogMuxMix0}, PortId{kAnalogMuxMix1}, PortId{kAnalogMuxAux}},
                .outputs = {PortId{kAnalogMuxOut}, PortId{kAnalogMuxOut + 1}},
                .legalBundles = std::move(analogBundles),
                .constraints = RouterConstraints{
                    .maxActiveBundles = kAnalogOutPairs,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = kAnalogOutPairs,
                },
            },
        },
        Node{
            nHeadphoneMux,
            "Headphone Pair Source",
            RouterNode{
                .inputs = {PortId{kPhoneMuxMix0}, PortId{kPhoneMuxMix1}, PortId{kPhoneMuxAux}},
                .outputs = {PortId{kPhoneMuxOut}, PortId{kPhoneMuxOut + 1}},
                .legalBundles = std::move(phoneBundles),
                .constraints = RouterConstraints{
                    .maxActiveBundles = kHeadphonePairs,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = kHeadphonePairs,
                },
            },
        },
        Node{nPhysOut, "Physical Outputs", EndpointNode{EndpointKind::Physical}},
    };

    // Only one digital input is live at a time. The Panel splits that choice
    // into format (S/PDIF vs ADAT) and connector (coaxial vs optical), and only
    // offers the connector control in S/PDIF format -- CFW1814HardwareView::
    // AdaptPortsToSettings disables the "active input" group when the optical
    // setting is ADAT. Linux encodes the same thing as one three-way selector,
    // (dig_in_fmt << 1) | iface, in bebob_maudio.c:441.
    if (geometry.hasDigitalInputSelector) {
        t.nodes.push_back(Node{
            nDigitalInMux,
            "Digital Input Source",
            RouterNode{
                .inputs = {PortId{kDigitalMuxCoax}, PortId{kDigitalMuxOptical}},
                .outputs = {PortId{kDigitalMuxOut}},
                .legalBundles = {
                    RouteBundle{RouteBundleId{1}, {Route{PortId{kDigitalMuxCoax}, PortId{kDigitalMuxOut}}}},
                    RouteBundle{RouteBundleId{2}, {Route{PortId{kDigitalMuxOptical}, PortId{kDigitalMuxOut}}}},
                },
                .constraints = RouterConstraints{
                    .maxActiveBundles = 1,
                    .maxSourcesPerOutput = 1,
                    .maxDestinationsPerInput = 1,
                },
            },
        });
    }

    // --- Fixed links -----------------------------------------------------

    // Capture is direct: it does not pass through the mixer
    // (documentation/1814.md 3.2b -- the main mixer has no host destination).
    if (geometry.hasDigitalInputSelector) {
        t.fixedLinks.push_back({PortId{kPhysSpdifCoaxIn}, PortId{kDigitalMuxCoax}});
        t.fixedLinks.push_back({PortId{kPhysDigitalIn}, PortId{kDigitalMuxOptical}});
    }
    for (uint32_t i = 0; i < capturePairs; ++i) {
        const PortId source = (i < kAnalogInPairs)
            ? PortId{kPhysAnalogIn + i}
            : digitalInputSource(geometry, i - kAnalogInPairs);
        t.fixedLinks.push_back({source, PortId{kHostCapture + i}});
        t.fixedLinks.push_back({source, PortId{kMainMixerIn + i}});
        t.fixedLinks.push_back({source, PortId{kAuxMixerIn + i}});
    }

    // Only the first two playback pairs reach the mixers; the rest are the
    // digital output slots and go straight to the connector.
    for (uint32_t i = 0; i < kStreamMixerPairs; ++i) {
        const uint32_t slot = kAnalogInPairs + geometry.digitalInPairs + i;
        t.fixedLinks.push_back({PortId{kHostPlayback + i}, PortId{kMainMixerIn + slot}});
        t.fixedLinks.push_back({PortId{kHostPlayback + i}, PortId{kAuxMixerIn + slot}});
    }
    for (uint32_t i = 0; i < geometry.digitalOutPairs; ++i) {
        t.fixedLinks.push_back({PortId{kHostPlayback + kStreamMixerPairs + i}, PortId{kPhysDigitalOut + i}});
    }

    t.fixedLinks.push_back({PortId{kMainMixerOut0}, PortId{kAnalogMuxMix0}});
    t.fixedLinks.push_back({PortId{kMainMixerOut1}, PortId{kAnalogMuxMix1}});
    t.fixedLinks.push_back({PortId{kAuxMixerOut}, PortId{kAnalogMuxAux}});
    t.fixedLinks.push_back({PortId{kMainMixerOut0}, PortId{kPhoneMuxMix0}});
    t.fixedLinks.push_back({PortId{kMainMixerOut1}, PortId{kPhoneMuxMix1}});
    t.fixedLinks.push_back({PortId{kAuxMixerOut}, PortId{kPhoneMuxAux}});

    for (uint32_t i = 0; i < kAnalogOutPairs; ++i) {
        t.fixedLinks.push_back({PortId{kAnalogMuxOut + i}, PortId{kPhysAnalogOut + i}});
    }
    for (uint32_t i = 0; i < kHeadphonePairs; ++i) {
        t.fixedLinks.push_back({PortId{kPhoneMuxOut + i}, PortId{kPhysHeadphone + i}});
    }

    // --- Channels and buses ----------------------------------------------

    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        std::vector<PortId> ports{PortId{kMainMixerIn + i}, PortId{kAuxMixerIn + i}};
        if (mixerInputIsPhysical(geometry, i)) {
            ports.push_back((i < kAnalogInPairs)
                ? PortId{kPhysAnalogIn + i}
                : digitalInputSource(geometry, i - kAnalogInPairs));
            ports.push_back(PortId{kHostCapture + i});
        } else {
            ports.push_back(PortId{kHostPlayback + (i - kAnalogInPairs - geometry.digitalInPairs)});
        }
        t.channels.push_back(Channel{
            .id = ChannelId{i + 1},
            .name = mixerInputName(geometry, i),
            .ports = std::move(ports),
        });
    }

    t.buses = {
        Bus{BusId{1}, BusSemantic::Main, "Mixer 1", {PortId{kMainMixerOut0}}},
        Bus{BusId{2}, BusSemantic::Main, "Mixer 2", {PortId{kMainMixerOut1}}},
        Bus{BusId{3}, BusSemantic::Aux, "Aux", {PortId{kAuxMixerOut}}},
    };

    // --- Parameters ------------------------------------------------------
    //
    // Level sits on the mixer *input port*, not on the crosspoint: the
    // parameter window holds one gain per input channel, shared by both mixer
    // outputs, while the crosspoints are the on/off bits of the 0x90/0x94
    // routing masks. This is AUAA 13.1 in practice.

    uint32_t parameterId = 1;

    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kMainMixerIn + i},
            .semantic = ParameterSemantic::Level,
            .domain = kGainDomain,
            .name = mixerInputName(geometry, i) + " Mixer Gain",
        });
    }
    // Balance exists for the physical inputs only; the parameter window has no
    // stream balance registers.
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        if (!mixerInputIsPhysical(geometry, i)) continue;
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kMainMixerIn + i},
            .semantic = ParameterSemantic::Balance,
            .domain = kBalanceDomain,
            .name = mixerInputName(geometry, i) + " Balance",
        });
    }
    for (const auto& crosspoint : std::get<MixerNode>(t.nodes[2].body).crosspoints) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = crosspoint.id,
            .semantic = ParameterSemantic::Mute,
            .domain = BooleanDomain{},
            .name = "Mixer Send Disabled",
        });
    }
    for (uint32_t i = 0; i < geometry.mixerPairs; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kAuxMixerIn + i},
            .semantic = ParameterSemantic::Level,
            .domain = kGainDomain,
            .name = mixerInputName(geometry, i) + " Aux Gain",
        });
    }

    t.parameters.push_back(Parameter{
        .id = ParameterId{parameterId++},
        .target = PortId{kAuxMixerOut},
        .semantic = ParameterSemantic::Level,
        .domain = kGainDomain,
        .name = "Aux Output Volume",
    });
    for (uint32_t i = 0; i < kAnalogOutPairs; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kPhysAnalogOut + i},
            .semantic = ParameterSemantic::Level,
            .domain = kGainDomain,
            .name = "Analog Out " + std::to_string(i * 2 + 1) + "/" + std::to_string(i * 2 + 2) + " Volume",
        });
    }
    for (uint32_t i = 0; i < kHeadphonePairs; ++i) {
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kPhysHeadphone + i},
            .semantic = ParameterSemantic::Level,
            .domain = kGainDomain,
            .name = "Headphone " + std::to_string(i + 1) + " Volume",
        });
    }
    // The digital outputs have levels too -- `fw vol` carries `adatout` and
    // `spdifout` blocks over the same -128..0 dB range, and `Adat Out Level` is
    // one of the rows `fw vol show` prints (documentation/1814.md 6.4).
    //
    // NOTE the provenance difference: unlike every other level here, these are
    // NOT in the 40-quadlet parameter window the ALSA crate models, so how a
    // driver actually sets them is unresolved. Modelled because the device has
    // them; the binding is the open part, not the control.
    for (uint32_t i = 0; i < geometry.digitalOutPairs; ++i) {
        const char* label = (geometry.digitalOutKind == SignalKind::Adat) ? "ADAT Out " : "Opt S/PDIF Out ";
        t.parameters.push_back(Parameter{
            .id = ParameterId{parameterId++},
            .target = PortId{kPhysDigitalOut + i},
            .semantic = ParameterSemantic::Level,
            .domain = kGainDomain,
            .name = label + std::to_string(i * 2 + 1) + "/" + std::to_string(i * 2 + 2) + " Volume",
        });
    }

    // Wire values from Linux bebob_maudio.c:342-348. Value 1 is "Digital",
    // whichever source the digital input interface selects -- the device does
    // not expose separate S/PDIF and ADAT clock sources.
    t.parameters.push_back(Parameter{
        .id = ParameterId{parameterId++},
        .target = nPhysIn,
        .semantic = ParameterSemantic::ClockSource,
        .domain = EnumDomain{
            .values = {
                EnumItem{0, "Internal with Digital Mute"},
                EnumItem{1, "Digital"},
                EnumItem{2, "Word Clock"},
                EnumItem{3, "Internal"},
            },
        },
        .name = "Clock Source",
    });

    // --- Meters ----------------------------------------------------------
    //
    // 19 stereo points, from Linux bebob_maudio.c:618-627 special_meter_labels.
    // Only those that exist in the current mode are published.

    uint32_t meterId = 1;
    auto addMeter = [&](PortId port, const std::string& name) {
        t.meters.push_back(Meter{
            .id = MeterId{meterId++},
            .target = port,
            .semantic = MeterSemantic::Peak,
            .domain = kMeterDomain,
            .name = name,
        });
    };
    for (uint32_t i = 0; i < capturePairs; ++i) {
        const PortId port = (i < kAnalogInPairs)
            ? PortId{kPhysAnalogIn + i}
            : digitalInputSource(geometry, i - kAnalogInPairs);
        addMeter(port, mixerInputName(geometry, i) + " Peak");
    }
    for (uint32_t i = 0; i < kAnalogOutPairs; ++i) {
        addMeter(PortId{kPhysAnalogOut + i}, "Analog Out " + std::to_string(i + 1) + " Peak");
    }
    for (uint32_t i = 0; i < kHeadphonePairs; ++i) {
        addMeter(PortId{kPhysHeadphone + i}, "Headphone " + std::to_string(i + 1) + " Peak");
    }
    for (uint32_t i = 0; i < geometry.digitalOutPairs; ++i) {
        addMeter(PortId{kPhysDigitalOut + i}, "Digital Out " + std::to_string(i + 1) + " Peak");
    }
    addMeter(PortId{kAuxMixerOut}, "Aux Out Peak");

    // --- Presentation ----------------------------------------------------

    resolved.presentation.mixers = {
        Presentation::MixerPresentationHint{
            .mixer = nMainMixer,
            .style = Presentation::MixerPresentationStyle::ChannelStrips,
        },
        Presentation::MixerPresentationHint{
            .mixer = nAuxMixer,
            .style = Presentation::MixerPresentationStyle::ChannelStrips,
        },
    };

    std::vector<Presentation::RouteBundleGroup> analogGroups;
    for (uint32_t pair = 0; pair < kAnalogOutPairs; ++pair) {
        analogGroups.push_back({
            "Analog Out " + std::to_string(pair * 2 + 1) + "/" + std::to_string(pair * 2 + 2) + " Source",
            {RouteBundleId{pair * 2 + 1}, RouteBundleId{pair * 2 + 2}},
        });
    }
    std::vector<Presentation::RouteBundleGroup> phoneGroups;
    for (uint32_t pair = 0; pair < kHeadphonePairs; ++pair) {
        phoneGroups.push_back({
            "Headphone " + std::to_string(pair + 1) + " Source",
            {RouteBundleId{pair * 3 + 1}, RouteBundleId{pair * 3 + 2}, RouteBundleId{pair * 3 + 3}},
        });
    }
    if (geometry.hasDigitalInputSelector) {
        resolved.presentation.routers.push_back(Presentation::RouterPresentationHint{
            .router = nDigitalInMux,
            .style = Presentation::RouterPresentationStyle::Selector,
            .bundleGroups = {{"Digital Input Source", {RouteBundleId{1}, RouteBundleId{2}}}},
        });
    }
    const std::vector<Presentation::RouterPresentationHint> outputRouters = {
        Presentation::RouterPresentationHint{
            .router = nAnalogOutMux,
            .style = Presentation::RouterPresentationStyle::Selector,
            .bundleGroups = std::move(analogGroups),
        },
        Presentation::RouterPresentationHint{
            .router = nHeadphoneMux,
            .style = Presentation::RouterPresentationStyle::Selector,
            .bundleGroups = std::move(phoneGroups),
        },
    };
    resolved.presentation.routers.insert(resolved.presentation.routers.end(),
                                         outputRouters.begin(), outputRouters.end());

    return resolved;
}

DeviceState makeInitialState(const ResolvedAudioConfiguration& resolved) {
    DeviceState state;
    const Topology& t = resolved.topology;

    for (const auto& parameter : t.parameters) {
        std::visit([&](const auto& domain) {
            using D = std::decay_t<decltype(domain)>;
            if constexpr (std::is_same_v<D, BooleanDomain>) {
                state.parameters[parameter.id] = false;
            } else if constexpr (std::is_same_v<D, ScalarDomain>) {
                // Gains are i16 with 0 = maximum (documentation/1814.md 6.1).
                state.parameters[parameter.id] =
                    (parameter.semantic == ParameterSemantic::Balance) ? 0.0 : domain.max;
            } else if constexpr (std::is_same_v<D, EnumDomain>) {
                // Internal, which is what Linux selects at discovery
                // (bebob_maudio.c:276 sends clk_src 0x03).
                const int64_t fallback = domain.values.empty() ? 0 : domain.values.front().value;
                state.parameters[parameter.id] =
                    (parameter.semantic == ParameterSemantic::ClockSource) ? int64_t{3} : fallback;
            }
        }, parameter.domain);
    }

    // The device powers up with every physical send disabled and only the two
    // stream pairs routed -- 0x90 defaults to 0 and 0x94 to 0x00000009. That
    // empty mixer is what made the unit silent until the driver asserted the
    // routing (documentation/1814.md 6.2, 2.2).
    const auto& mainMixer = std::get<MixerNode>(t.nodes[2].body);
    const uint32_t mixerPairs = static_cast<uint32_t>(mainMixer.inputs.size());
    const uint32_t streamFirst = mixerPairs - kStreamMixerPairs;
    for (const auto& crosspoint : mainMixer.crosspoints) {
        for (const auto& parameter : t.parameters) {
            if (!std::holds_alternative<CrosspointId>(parameter.target)) continue;
            if (std::get<CrosspointId>(parameter.target) != crosspoint.id) continue;

            const uint32_t input = crosspoint.input.value - kMainMixerIn;
            const bool isStream = input >= streamFirst;
            const uint32_t streamPair = isStream ? input - streamFirst : 0;
            const bool toMixer0 = crosspoint.output.value == kMainMixerOut0;
            const bool routed = isStream && (toMixer0 ? streamPair == 0 : streamPair == 1);
            state.parameters[parameter.id] = !routed;  // parameter is "send disabled"
        }
    }

    for (const auto& meter : t.meters) {
        state.meters[meter.id] = kMeterDomain.min;
    }

    // Analog pairs take their own mixer output; headphone pairs follow the
    // mixer pairs (0x9c = 0x00000000, 0x98 = 0x00020001).
    for (const auto& node : t.nodes) {
        if (node.name == "Digital Input Source") {
            // Coaxial: the driver hardcodes S/PDIF today and the Panel's own
            // default is the first button.
            state.routers[node.id] = RouterState{.activeBundles = {RouteBundleId{1}}};
        }
    }
    state.routers[nAnalogOutMux] = RouterState{.activeBundles = {RouteBundleId{1}, RouteBundleId{3}}};
    state.routers[nHeadphoneMux] = RouterState{.activeBundles = {RouteBundleId{1}, RouteBundleId{5}}};

    return state;
}

} // namespace ASFW::Devices::FW1814
