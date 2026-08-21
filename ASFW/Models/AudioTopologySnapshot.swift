import Foundation

/// Device-neutral projection consumed by the console and patchbay views.
///
/// A protocol family translates its confirmed hardware state into this model;
/// the views never receive register offsets or family-specific parameter
/// windows.
///
/// The unit here is the **channel**, not the pair. The device's registers are
/// per-channel and the vendor's own console exposes them that way — two faders,
/// two pans, two aux sends per strip, with a link button to gang them. Folding a
/// pair behind one fader loses the balance the hardware can actually do.
struct AudioTopologySnapshot {
    let revision: UInt32
    let strips: [AudioTopologyStrip]
    let routes: [AudioTopologyRoute]
}

enum AudioTopologyStripKind {
    case physicalInput
    case playback
    case output
    case aux
    case headphone

    var isInput: Bool { self == .physicalInput || self == .playback }
}

struct AudioTopologyStrip: Identifiable {
    let id: String
    let name: String
    let kind: AudioTopologyStripKind
    let channels: [AudioTopologyStripChannel]
    /// Mixer destination buttons — the vendor labels these "out 1/2" and "3/4".
    let sends: [AudioTopologySend]
    /// Source selector shown on the strip itself, as the vendor puts "mon"
    /// under the headphone faders rather than in a separate patchbay.
    let source: AudioTopologyRoute?
}

struct AudioTopologyStripChannel: Identifiable {
    let id: String
    /// "L" / "R", or the channel number for a mono strip.
    let label: String
    let levelControl: MAudio1814ControlID
    let levelRaw: Int32
    /// Pan into the main mixer. Absent for playback and output strips, which
    /// have no balance register.
    let panControl: MAudio1814ControlID?
    let panRaw: Int32
    /// Send into the aux bus. Absent on output strips.
    let auxControl: MAudio1814ControlID?
    let auxRaw: Int32
    /// Index into the 38-point peak block.
    let meterIndex: Int?
}

struct AudioTopologySend: Identifiable {
    let id: String
    let label: String
    let control: MAudio1814ControlID
    let mask: UInt32
    let isEnabled: Bool
}

struct AudioTopologyRoute: Identifiable {
    let id: String
    let name: String
    let control: MAudio1814ControlID
    let choices: [AudioTopologyRouteChoice]
    let selectedValue: Int32
}

struct AudioTopologyRouteChoice: Identifiable {
    let value: Int32
    let name: String
    var id: Int32 { value }
}

enum MAudio1814TopologyProjector {
    static func make(configuration: AudioConfigurationSnapshot,
                     controls: AudioControlSurfaceSnapshot) -> AudioTopologySnapshot {
        let opticalIsADAT = configuration.committed.inputOptical == .adat

        var strips: [AudioTopologyStrip] = []

        // Analog inputs: four pairs, always present.
        for pair in 0..<4 {
            strips.append(inputStrip(
                id: "analog-\(pair)", name: "ANALOG \(pair * 2 + 1)/\(pair * 2 + 2) IN",
                gain: .mixerAnalogGain, pan: .mixerAnalogBalance, aux: .auxAnalogGain,
                pair: UInt32(pair), sendBit: UInt32(pair), meterFirst: pair * 2,
                controls: controls))
        }

        if opticalIsADAT {
            for pair in 0..<4 {
                strips.append(inputStrip(
                    id: "adat-\(pair)", name: "ADAT \(pair * 2 + 1)/\(pair * 2 + 2) IN",
                    gain: .mixerAdatGain, pan: .mixerAdatBalance, aux: .auxAdatGain,
                    pair: UInt32(pair), sendBit: UInt32(8 + pair), meterFirst: 10 + pair * 2,
                    controls: controls))
            }
        } else {
            strips.append(inputStrip(
                id: "spdif-0", name: "S/PDIF IN",
                gain: .mixerSpdifGain, pan: .mixerSpdifBalance, aux: .auxSpdifGain,
                pair: 0, sendBit: 16, meterFirst: 8, controls: controls))
        }

        // Software returns. These have a mixer gain and an aux send but no
        // balance register, which is why the vendor's "sw rtn" strips carry no
        // pan knobs.
        for pair in 0..<2 {
            strips.append(playbackStrip(pair: UInt32(pair), controls: controls))
        }

        strips.append(outputStrip(
            id: "analog-out-0", name: "1/2 OUT", kind: .output,
            group: .analogOutputVolume, pair: 0, meterFirst: 18, controls: controls,
            source: route("Source", MAudio1814ControlID(.analogOutputSource, 0),
                          controls, ["Mixer 1", "Aux"])))
        strips.append(outputStrip(
            id: "analog-out-1", name: "3/4 OUT", kind: .output,
            group: .analogOutputVolume, pair: 1, meterFirst: 20, controls: controls,
            source: route("Source", MAudio1814ControlID(.analogOutputSource, 1),
                          controls, ["Mixer 2", "Aux"])))
        strips.append(outputStrip(
            id: "aux-out", name: "AUX", kind: .aux,
            group: .auxOutputVolume, pair: 0, meterFirst: 36, controls: controls,
            source: nil))
        strips.append(outputStrip(
            id: "phones-0", name: "PHONES 1", kind: .headphone,
            group: .headphoneVolume, pair: 0, meterFirst: 32, controls: controls,
            source: route("Mon", MAudio1814ControlID(.headphoneSource, 0),
                          controls, ["1/2", "3/4", "Aux"])))
        strips.append(outputStrip(
            id: "phones-1", name: "PHONES 2", kind: .headphone,
            group: .headphoneVolume, pair: 1, meterFirst: 34, controls: controls,
            source: route("Mon", MAudio1814ControlID(.headphoneSource, 1),
                          controls, ["1/2", "3/4", "Aux"])))

        return AudioTopologySnapshot(
            revision: controls.revision,
            strips: strips,
            routes: strips.compactMap(\.source))
    }

    // MARK: - Strip builders

    private static func inputStrip(id: String, name: String,
                                   gain: MAudio1814ControlGroup,
                                   pan: MAudio1814ControlGroup,
                                   aux: MAudio1814ControlGroup,
                                   pair: UInt32, sendBit: UInt32, meterFirst: Int,
                                   controls: AudioControlSurfaceSnapshot) -> AudioTopologyStrip {
        AudioTopologyStrip(
            id: id, name: name, kind: .physicalInput,
            channels: (0..<2).map { side in
                let index = pair * 2 + UInt32(side)
                return channel(
                    id: "\(id)-\(side)", label: side == 0 ? "L" : "R",
                    level: MAudio1814ControlID(gain, index),
                    pan: MAudio1814ControlID(pan, index),
                    aux: MAudio1814ControlID(aux, index),
                    meterIndex: meterFirst + side, controls: controls)
            },
            sends: sends(prefix: id, control: MAudio1814ControlID(.physicalMixerSendMask),
                         // Analog and ADAT sit in four-bit fields so their second
                         // destination is four bits along; S/PDIF's is adjacent.
                         firstBit: sendBit, secondBit: sendBit + (sendBit < 8 ? 4 : 1),
                         controls: controls),
            source: nil)
    }

    private static func playbackStrip(pair: UInt32,
                                      controls: AudioControlSurfaceSnapshot) -> AudioTopologyStrip {
        let id = "playback-\(pair)"
        return AudioTopologyStrip(
            id: id, name: "\(pair * 2 + 1)/\(pair * 2 + 2) SW RTN", kind: .playback,
            channels: (0..<2).map { side in
                let index = pair * 2 + UInt32(side)
                return channel(
                    id: "\(id)-\(side)", label: side == 0 ? "L" : "R",
                    level: MAudio1814ControlID(.mixerStreamGain, index),
                    pan: nil,
                    aux: MAudio1814ControlID(.auxStreamGain, index),
                    meterIndex: nil, controls: controls)
            },
            sends: sends(prefix: id, control: MAudio1814ControlID(.streamMixerSendMask),
                         firstBit: pair, secondBit: pair + 2, controls: controls),
            source: nil)
    }

    private static func outputStrip(id: String, name: String, kind: AudioTopologyStripKind,
                                    group: MAudio1814ControlGroup, pair: UInt32,
                                    meterFirst: Int, controls: AudioControlSurfaceSnapshot,
                                    source: AudioTopologyRoute?) -> AudioTopologyStrip {
        AudioTopologyStrip(
            id: id, name: name, kind: kind,
            channels: (0..<2).map { side in
                channel(
                    id: "\(id)-\(side)", label: side == 0 ? "L" : "R",
                    level: MAudio1814ControlID(group, pair * 2 + UInt32(side)),
                    pan: nil, aux: nil,
                    meterIndex: meterFirst + side, controls: controls)
            },
            sends: [], source: source)
    }

    private static func channel(id: String, label: String,
                                level: MAudio1814ControlID,
                                pan: MAudio1814ControlID?,
                                aux: MAudio1814ControlID?,
                                meterIndex: Int?,
                                controls: AudioControlSurfaceSnapshot) -> AudioTopologyStripChannel {
        AudioTopologyStripChannel(
            id: id, label: label,
            levelControl: level, levelRaw: controls.value(for: level),
            panControl: pan, panRaw: pan.map { controls.value(for: $0) } ?? 0,
            auxControl: aux, auxRaw: aux.map { controls.value(for: $0) } ?? 0,
            meterIndex: meterIndex)
    }

    /// The two mixer destinations. The vendor labels these by the output pair
    /// they feed rather than by mixer number.
    private static func sends(prefix: String, control: MAudio1814ControlID,
                              firstBit: UInt32, secondBit: UInt32,
                              controls: AudioControlSurfaceSnapshot) -> [AudioTopologySend] {
        let value = UInt32(bitPattern: controls.value(for: control))
        return [
            AudioTopologySend(id: "\(prefix)-out12", label: "1/2", control: control,
                              mask: 1 << firstBit, isEnabled: value & (1 << firstBit) != 0),
            AudioTopologySend(id: "\(prefix)-out34", label: "3/4", control: control,
                              mask: 1 << secondBit, isEnabled: value & (1 << secondBit) != 0),
        ]
    }

    private static func route(_ name: String, _ control: MAudio1814ControlID,
                              _ controls: AudioControlSurfaceSnapshot,
                              _ names: [String]) -> AudioTopologyRoute {
        AudioTopologyRoute(
            id: "\(control.rawValue)", name: name, control: control,
            choices: names.enumerated().map {
                AudioTopologyRouteChoice(value: Int32($0.offset), name: $0.element)
            },
            selectedValue: controls.value(for: control))
    }
}
