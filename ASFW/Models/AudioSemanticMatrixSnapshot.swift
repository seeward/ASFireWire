import Foundation

/// Dense driver-owned mixer state. Axis identities are semantic labels, not
/// DICE router block/channel fields or coefficient-register offsets.
nonisolated struct AudioSemanticMatrixSnapshot: Equatable, Sendable {
    struct Axis: Identifiable, Equatable, Sendable {
        let portID: UInt32
        let signalKind: AudioSemanticTopologySnapshot.SignalKind
        let signalIndex: UInt32

        var id: UInt32 { portID }
    }

    let endpointID: AudioEndpointID
    let deviceKind: UInt32
    let topologyRevision: UInt64
    let stateRevision: UInt32
    let coefficientMaximum: UInt16
    let inputs: [Axis]
    let outputs: [Axis]
    /// Row-major output × input gain cells, exactly as described by the
    /// semantic axes. The UI must not make an assumption about a vendor stride.
    let coefficients: [UInt16]

    func coefficient(output: Int, input: Int) -> UInt16? {
        guard outputs.indices.contains(output), inputs.indices.contains(input) else { return nil }
        return coefficients[output * inputs.count + input]
    }
}
