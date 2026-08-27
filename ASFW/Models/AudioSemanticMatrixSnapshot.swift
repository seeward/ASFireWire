import Foundation

/// Dense driver-owned mixer state. Axis identities are semantic labels, not
/// DICE router block/channel fields or coefficient-register offsets.
nonisolated struct AudioSemanticMatrixSnapshot: Equatable, Sendable {
    enum GainLaw: UInt16, Equatable, Sendable {
        case linearNormalized = 1
        case unsignedQ214Amplitude = 2
    }

    enum ChannelRole: UInt8, Equatable, Sendable {
        case mono = 1
        case left = 2
        case right = 3
    }

    enum OutputRole: UInt8, Equatable, Sendable {
        case monitorMix = 1
        case effectSend = 2
    }

    /// Driver-owned meaning of one coefficient cell. The dense coefficient
    /// image is complete readback; this map decides which cells form a real
    /// product control and which must not be plotted or written.
    enum CrosspointPresentation: UInt8, Equatable, Sendable {
        case hidden = 0
        case scalarReadback = 1
        case monoLevelPan = 2
        case stereoLevelBalance = 3
    }

    /// Sparse: a strip with no record is neither muted nor soloed, and its
    /// nominal level is simply its live coefficient.
    struct StripState: Equatable, Sendable {
        let outputPresentationGroupID: UInt32
        let inputPresentationGroupID: UInt32
        let nominalLeft: UInt16
        let nominalRight: UInt16
        let muted: Bool
        let soloed: Bool
    }

    struct Axis: Identifiable, Equatable, Sendable {
        let portID: UInt32
        let signalKind: AudioSemanticTopologySnapshot.SignalKind
        let signalIndex: UInt32
        let presentationGroupID: UInt32
        let channelRole: ChannelRole
        /// Present only on an output axis. Inputs never expose a destination role.
        let outputRole: OutputRole?

        var id: UInt32 { portID }
    }

    let endpointID: AudioEndpointID
    let deviceKind: UInt32
    let topologyRevision: UInt64
    let stateRevision: UInt32
    let coefficientMaximum: UInt16
    let gainLaw: GainLaw
    let inputs: [Axis]
    let outputs: [Axis]
    /// Row-major output × input gain cells, exactly as described by the
    /// semantic axes. The UI must not make an assumption about a vendor stride.
    let coefficients: [UInt16]
    let crosspointPresentations: [CrosspointPresentation]
    /// Mute, solo and the level a strip returns to. The device cannot report
    /// any of it -- a coefficient of zero is what a mute is -- so this is the
    /// driver's own state and the only place the distinction exists.
    let stripStates: [StripState]

    func coefficient(output: Int, input: Int) -> UInt16? {
        guard outputs.indices.contains(output), inputs.indices.contains(input) else { return nil }
        return coefficients[output * inputs.count + input]
    }

    func crosspointPresentation(output: Int, input: Int) -> CrosspointPresentation? {
        guard outputs.indices.contains(output), inputs.indices.contains(input) else { return nil }
        return crosspointPresentations[output * inputs.count + input]
    }

    func stripState(output: UInt32, input: UInt32) -> StripState? {
        stripStates.first { $0.outputPresentationGroupID == output && $0.inputPresentationGroupID == input }
    }

    /// A soloed strip anywhere on a bus is what silences that bus's others.
    func busHasSolo(output: UInt32) -> Bool {
        stripStates.contains { $0.outputPresentationGroupID == output && $0.soloed }
    }

    func stripIsSuppressed(output: UInt32, input: UInt32) -> Bool {
        let state = stripState(output: output, input: input)
        if state?.muted == true { return true }
        guard busHasSolo(output: output) else { return false }
        return state?.soloed != true
    }
}
