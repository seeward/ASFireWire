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
    let levelControl: MAudio1814ControlID
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

        var channels = (0..<physicalPairCount).map { pair in
            let name: String
            let meterPair: Int
            let mask: UInt32
            if pair < 4 {
                name = "LINE IN \(pair * 2 + 1)/\(pair * 2 + 2)"
                meterPair = pair
                mask = UInt32(pair)
            } else if opticalIsADAT {
                let adatPair = pair - 4
                name = "ADAT IN \(adatPair * 2 + 1)/\(adatPair * 2 + 2)"
                meterPair = 5 + adatPair
                mask = UInt32(8 + adatPair)
            } else {
                name = "S/PDIF IN 1/2"
                meterPair = 4
                mask = 16
            }
            return AudioTopologyChannel(
                id: "physical-\(pair)", name: name, kind: .physical,
                sendControls: physicalSends(mask: mask, controls: controls), meterPair: meterPair)
        }

        channels += [
            playbackChannel(pair: 0, controls: controls),
            playbackChannel(pair: 1, controls: controls),
        ]

        let outputs = [
            output("ANALOG OUT 1/2", .analogOutput12Level, value: controls.value(for: 1), meterPair: 9),
            output("ANALOG OUT 3/4", .analogOutput34Level, value: controls.value(for: 2), meterPair: 10),
            output("HEADPHONE 1/2", .headphone12Level, value: controls.value(for: 3), meterPair: 16),
            output("HEADPHONE 3/4", .headphone34Level, value: controls.value(for: 4), meterPair: 17),
        ]

        return AudioTopologySnapshot(
            revision: max(configuration.committed.sampleRateHz, controls.revision),
            channels: channels,
            outputMasters: outputs,
            routes: [
                route("Analog Out 1/2 Source", .analogOutput12Source, controls.value(for: 5), ["Mixer 1", "Aux"]),
                route("Analog Out 3/4 Source", .analogOutput34Source, controls.value(for: 6), ["Mixer 2", "Aux"]),
                route("Headphone 1/2 Source", .headphone12Source, controls.value(for: 7), ["Mixer 1", "Mixer 2", "Aux"]),
                route("Headphone 3/4 Source", .headphone34Source, controls.value(for: 8), ["Mixer 1", "Mixer 2", "Aux"]),
            ]
        )
    }

    private static func physicalSends(mask: UInt32, controls: AudioControlSurfaceSnapshot) -> [AudioTopologySend] {
        let value = UInt32(bitPattern: controls.value(for: MAudio1814ControlID.physicalMixerSendMask.rawValue))
        return [
            AudioTopologySend(id: "physical-\(mask)-m1", label: "M1", control: .physicalMixerSendMask,
                              mask: 1 << mask, isEnabled: value & (1 << mask) != 0),
            AudioTopologySend(id: "physical-\(mask)-m2", label: "M2", control: .physicalMixerSendMask,
                              mask: 1 << (mask + (mask < 8 ? 4 : 1)),
                              isEnabled: value & (1 << (mask + (mask < 8 ? 4 : 1))) != 0),
        ]
    }

    private static func playbackChannel(pair: UInt32, controls: AudioControlSurfaceSnapshot) -> AudioTopologyChannel {
        let value = UInt32(bitPattern: controls.value(for: MAudio1814ControlID.streamMixerSendMask.rawValue))
        let m1Mask: UInt32 = UInt32(1) << pair
        let m2Mask: UInt32 = UInt32(1) << (pair + 2)
        return AudioTopologyChannel(
            id: "playback-\(pair)", name: "PLAYBACK \(pair * 2 + 1)/\(pair * 2 + 2)", kind: .playback,
            sendControls: [
                AudioTopologySend(id: "playback-\(pair)-m1", label: "M1", control: .streamMixerSendMask,
                                  mask: m1Mask, isEnabled: value & m1Mask != 0),
                AudioTopologySend(id: "playback-\(pair)-m2", label: "M2", control: .streamMixerSendMask,
                                  mask: m2Mask, isEnabled: value & m2Mask != 0),
            ], meterPair: nil)
    }

    private static func output(_ name: String, _ control: MAudio1814ControlID,
                               value: Int32, meterPair: Int) -> AudioTopologyOutputMaster {
        AudioTopologyOutputMaster(id: name, name: name, levelControl: control,
                                  level: levelPercent(value), meterPair: meterPair)
    }

    private static func route(_ name: String, _ control: MAudio1814ControlID,
                              _ selectedValue: Int32, _ names: [String]) -> AudioTopologyRoute {
        AudioTopologyRoute(id: name, name: name, control: control,
                           choices: names.enumerated().map { AudioTopologyRouteChoice(value: Int32($0.offset), name: $0.element) },
                           selectedValue: selectedValue)
    }

    static func rawLevel(percent: Double) -> Int32 {
        Int32((max(0, min(100, percent)) * 327.68).rounded()) - 32_768
    }

    private static func levelPercent(_ value: Int32) -> Double {
        max(0, min(100, Double(value + 32_768) * 100 / 32_768))
    }
}
