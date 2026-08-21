import Foundation

/// Device-neutral projection consumed by the console and patchbay views.
///
/// A protocol family translates its confirmed hardware state into this model;
/// the views never receive register offsets or family-specific parameter
/// windows.  This is deliberately the same boundary as the lab's semantic
/// topology → generic presenter split, pared down to the controls the real
/// 1814 backend currently implements.
struct AudioTopologySnapshot {
    let revision: UInt32
    let channels: [AudioTopologyChannel]
    let outputMasters: [AudioTopologyOutputMaster]
    let routes: [AudioTopologyRoute]
}

struct AudioTopologyChannel: Identifiable {
    let id: String
    let name: String
    let kind: AudioTopologyChannelKind
    let sendControls: [AudioTopologySend]
    let meterPair: Int?
    /// Gain into the main mixer. Both channels of the pair, written together.
    let gainControls: [MAudio1814ControlID]
    let gain: Double
    /// Gain into the aux bus, which is what the headphones hear when their
    /// source is Aux.
    let auxControls: [MAudio1814ControlID]
    let auxSend: Double
    /// Stereo spread. The pair's two balance registers move in opposition, so
    /// 100% is the hard-panned factory default and 0% collapses it to mono.
    /// Empty for playback pairs — the device has no balance register for them.
    let widthControls: [MAudio1814ControlID]
    let width: Double
}

enum AudioTopologyChannelKind {
    case physical
    case playback
}

struct AudioTopologySend: Identifiable {
    let id: String
    let label: String
    let control: MAudio1814ControlID
    let mask: UInt32
    let isEnabled: Bool
}

struct AudioTopologyOutputMaster: Identifiable {
    let id: String
    let name: String
    let levelControls: [MAudio1814ControlID]
    let level: Double
    let meterPair: Int?
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
                     controls: AudioControlSurfaceSnapshot,
                     meters: AudioMeterSnapshot?) -> AudioTopologySnapshot {
        let opticalIsADAT = configuration.committed.inputOptical == .adat
        let physicalPairCount = opticalIsADAT ? 8 : 5

        var channels = (0..<physicalPairCount).map { pair -> AudioTopologyChannel in
            let name: String
            let meterPair: Int
            let mask: UInt32
            // Which register group and which pair within it. The device keeps
            // analog, S/PDIF and ADAT in separate ranges, so the console's flat
            // pair index has to be resolved back to one of them.
            let gainGroup: MAudio1814ControlGroup
            let auxGroup: MAudio1814ControlGroup
            let balanceGroup: MAudio1814ControlGroup
            let pairWithinGroup: UInt32

            if pair < 4 {
                name = "LINE IN \(pair * 2 + 1)/\(pair * 2 + 2)"
                meterPair = pair
                mask = UInt32(pair)
                (gainGroup, auxGroup, balanceGroup) =
                    (.mixerAnalogGain, .auxAnalogGain, .mixerAnalogBalance)
                pairWithinGroup = UInt32(pair)
            } else if opticalIsADAT {
                let adatPair = pair - 4
                name = "ADAT IN \(adatPair * 2 + 1)/\(adatPair * 2 + 2)"
                meterPair = 5 + adatPair
                mask = UInt32(8 + adatPair)
                (gainGroup, auxGroup, balanceGroup) =
                    (.mixerAdatGain, .auxAdatGain, .mixerAdatBalance)
                pairWithinGroup = UInt32(adatPair)
            } else {
                name = "S/PDIF IN 1/2"
                meterPair = 4
                mask = 16
                (gainGroup, auxGroup, balanceGroup) =
                    (.mixerSpdifGain, .auxSpdifGain, .mixerSpdifBalance)
                pairWithinGroup = 0
            }

            let gains = pairControls(gainGroup, pairWithinGroup)
            let auxes = pairControls(auxGroup, pairWithinGroup)
            let balances = pairControls(balanceGroup, pairWithinGroup)
            return AudioTopologyChannel(
                id: "physical-\(pair)", name: name, kind: .physical,
                sendControls: physicalSends(mask: mask, controls: controls),
                meterPair: meterPair,
                gainControls: gains,
                gain: levelPercent(controls.value(for: gains[0])),
                auxControls: auxes,
                auxSend: levelPercent(controls.value(for: auxes[0])),
                widthControls: balances,
                width: widthPercent(controls.value(for: balances[0])))
        }

        channels += [
            playbackChannel(pair: 0, controls: controls),
            playbackChannel(pair: 1, controls: controls),
        ]

        let outputs = [
            output("ANALOG OUT 1/2", .analogOutputVolume, 0, controls: controls, meterPair: 9),
            output("ANALOG OUT 3/4", .analogOutputVolume, 1, controls: controls, meterPair: 10),
            output("HEADPHONE 1/2", .headphoneVolume, 0, controls: controls, meterPair: 16),
            output("HEADPHONE 3/4", .headphoneVolume, 1, controls: controls, meterPair: 17),
            output("AUX OUT 1/2", .auxOutputVolume, 0, controls: controls, meterPair: 18),
        ]

        return AudioTopologySnapshot(
            revision: max(configuration.committed.sampleRateHz, controls.revision),
            channels: channels,
            outputMasters: outputs,
            routes: [
                route("Analog Out 1/2 Source", MAudio1814ControlID(.analogOutputSource, 0),
                      controls, ["Mixer 1", "Aux"]),
                route("Analog Out 3/4 Source", MAudio1814ControlID(.analogOutputSource, 1),
                      controls, ["Mixer 2", "Aux"]),
                route("Headphone 1/2 Source", MAudio1814ControlID(.headphoneSource, 0),
                      controls, ["Mixer 1", "Mixer 2", "Aux"]),
                route("Headphone 3/4 Source", MAudio1814ControlID(.headphoneSource, 1),
                      controls, ["Mixer 1", "Mixer 2", "Aux"]),
            ]
        )
    }

    /// The two register indices belonging to one stereo pair.
    private static func pairControls(_ group: MAudio1814ControlGroup,
                                     _ pair: UInt32) -> [MAudio1814ControlID] {
        [MAudio1814ControlID(group, pair * 2), MAudio1814ControlID(group, pair * 2 + 1)]
    }

    private static func physicalSends(mask: UInt32,
                                      controls: AudioControlSurfaceSnapshot) -> [AudioTopologySend] {
        let control = MAudio1814ControlID(.physicalMixerSendMask)
        let value = UInt32(bitPattern: controls.value(for: control))
        // Analog and ADAT occupy four-bit fields, so their mixer-2 bit is four
        // along; S/PDIF has a two-bit field and its mixer-2 bit is adjacent.
        let secondBit = mask + (mask < 8 ? 4 : 1)
        return [
            AudioTopologySend(id: "physical-\(mask)-m1", label: "M1", control: control,
                              mask: 1 << mask, isEnabled: value & (1 << mask) != 0),
            AudioTopologySend(id: "physical-\(mask)-m2", label: "M2", control: control,
                              mask: 1 << secondBit, isEnabled: value & (1 << secondBit) != 0),
        ]
    }

    private static func playbackChannel(pair: UInt32,
                                        controls: AudioControlSurfaceSnapshot) -> AudioTopologyChannel {
        let control = MAudio1814ControlID(.streamMixerSendMask)
        let value = UInt32(bitPattern: controls.value(for: control))
        let m1Mask: UInt32 = UInt32(1) << pair
        let m2Mask: UInt32 = UInt32(1) << (pair + 2)
        let gains = pairControls(.mixerStreamGain, pair)
        let auxes = pairControls(.auxStreamGain, pair)
        return AudioTopologyChannel(
            id: "playback-\(pair)", name: "PLAYBACK \(pair * 2 + 1)/\(pair * 2 + 2)",
            kind: .playback,
            sendControls: [
                AudioTopologySend(id: "playback-\(pair)-m1", label: "M1", control: control,
                                  mask: m1Mask, isEnabled: value & m1Mask != 0),
                AudioTopologySend(id: "playback-\(pair)-m2", label: "M2", control: control,
                                  mask: m2Mask, isEnabled: value & m2Mask != 0),
            ],
            meterPair: nil,
            gainControls: gains,
            gain: levelPercent(controls.value(for: gains[0])),
            auxControls: auxes,
            auxSend: levelPercent(controls.value(for: auxes[0])),
            widthControls: [],
            width: 0)
    }

    private static func output(_ name: String, _ group: MAudio1814ControlGroup, _ pair: UInt32,
                               controls: AudioControlSurfaceSnapshot,
                               meterPair: Int) -> AudioTopologyOutputMaster {
        let levels = pairControls(group, pair)
        return AudioTopologyOutputMaster(
            id: name, name: name, levelControls: levels,
            level: levelPercent(controls.value(for: levels[0])), meterPair: meterPair)
    }

    private static func route(_ name: String, _ control: MAudio1814ControlID,
                              _ controls: AudioControlSurfaceSnapshot,
                              _ names: [String]) -> AudioTopologyRoute {
        AudioTopologyRoute(
            id: name, name: name, control: control,
            choices: names.enumerated().map {
                AudioTopologyRouteChoice(value: Int32($0.offset), name: $0.element)
            },
            selectedValue: controls.value(for: control))
    }

    static func rawLevel(percent: Double) -> Int32 {
        Int32((max(0, min(100, percent)) * 327.68).rounded()) - 32_768
    }

    private static func levelPercent(_ value: Int32) -> Double {
        max(0, min(100, Double(value + 32_768) * 100 / 32_768))
    }

    /// Width is carried by the pair's two opposed balance registers. The first
    /// channel holds the positive extreme at full width, so its magnitude is the
    /// spread; the second is written as its negation.
    static func rawWidth(percent: Double, channel: Int) -> Int32 {
        let magnitude = Int32((max(0, min(100, percent)) * 326.40).rounded())
        return channel == 0 ? magnitude : -magnitude
    }

    private static func widthPercent(_ firstChannelValue: Int32) -> Double {
        max(0, min(100, Double(abs(Int(firstChannelValue))) * 100 / 32_640))
    }
}
