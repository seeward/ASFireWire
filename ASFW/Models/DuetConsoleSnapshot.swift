import Foundation

/// The complete, presentation-ready control surface of the Duet's fixed
/// two-input, four-source stereo monitor mixer.  It is deliberately derived
/// from the driver's semantic graph so the view never depends on AV/C or FCP
/// control numbers.
nonisolated struct DuetConsoleSnapshot: Equatable, Sendable {
    struct Control: Identifiable, Equatable, Sendable {
        let parameter: AudioSemanticTopologySnapshot.Parameter
        let value: Int32

        var id: UInt32 { parameter.id }
    }

    struct InputStrip: Identifiable, Equatable, Sendable {
        let id: UInt32
        let name: String
        let source: Control
        let gain: Control?
        let phantomPower: Control
        let phaseInvert: Control
        let nominalLevel: Control
        let meterLevel: Int16
        let hardwareSelected: Bool

        var isInstrument: Bool { source.value == 1 }
        var isFixedLevel: Bool { !isInstrument && nominalLevel.value != 0 }
    }

    struct MixerSend: Identifiable, Equatable, Sendable {
        let id: UInt32
        let destinationName: String
        let level: Control
    }

    /// One intended stereo strip in the cue rack. `routingSends` keeps the
    /// off-diagonal crosspoints available to a routing affordance without
    /// pretending each mono source is a second stereo strip.
    struct MixerStrip: Identifiable, Equatable, Sendable {
        let id: UInt32
        let name: String
        let sends: [MixerSend]
        let meterLevels: [Int16]
        let routingSends: [MixerSend]
    }

    struct MainOutput: Equatable, Sendable {
        let level: Control
        let mute: Control
        let source: Control
        let nominalLevel: Control
        let mainMuteFollow: Control
        let headphoneMuteFollow: Control
        let meterLevels: [Int16]
        let hardwareSelected: Bool
    }

    let inputs: [InputStrip]
    let mixerStrips: [MixerStrip]
    let mainOutput: MainOutput
    let stereoLink: Control
    let hardwareTarget: HardwareTarget
    let meteringEnabled: Bool

    enum HardwareTarget: Int32, Equatable, Sendable {
        case mainOutput = 0
        case input1 = 1
        case input2 = 2

        var label: String {
            switch self {
            case .mainOutput: "Main Output"
            case .input1: "Input 1"
            case .input2: "Input 2"
            }
        }
    }
}
