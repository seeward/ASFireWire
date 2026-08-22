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
        let gain: Control
        let phantomPower: Control
        let phaseInvert: Control
        let nominalLevel: Control
    }

    struct MixerSend: Identifiable, Equatable, Sendable {
        let id: UInt32
        let destinationName: String
        let level: Control
    }

    struct MixerSource: Identifiable, Equatable, Sendable {
        let id: UInt32
        let name: String
        let sends: [MixerSend]
    }

    struct MainOutput: Equatable, Sendable {
        let level: Control
        let mute: Control
    }

    let inputs: [InputStrip]
    let mixerSources: [MixerSource]
    let mainOutput: MainOutput
}
