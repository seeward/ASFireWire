import Foundation

/// Compact, driver-owned console projection for devices whose complete
/// control/meter vocabulary is denser than the general graph reply. Control
/// IDs are opaque semantic bindings; Swift never derives M-Audio register
/// groups, send-mask positions, or meter offsets.
nonisolated struct AudioSemanticConsoleLayoutSnapshot: Equatable, Sendable {
    enum StripKind: UInt8, Sendable { case input = 1, playback = 2, output = 3, auxiliary = 4, headphone = 5 }
    enum SourceKind: UInt8, Sendable { case none = 0, mixerOrAux = 1, mixer12Mixer34OrAux = 2 }
    enum Bus: UInt8, Sendable { case main12 = 1, main34 = 2 }

    struct Strip: Identifiable, Equatable, Sendable {
        let id: UInt32
        let kind: StripKind
        let signalKind: AudioSemanticTopologySnapshot.SignalKind
        let channelCount: UInt8
        let flags: UInt8
        let firstSignalIndex: UInt32
        let levelControlID: UInt32
        let panControlID: UInt32
        let auxControlID: UInt32
        let sourceControlID: UInt32
        let sourceKind: SourceKind
        let meterCount: UInt8
        let meterFirstIndex: UInt16

        var isLinkable: Bool { flags & 1 != 0 }
        var isMuteable: Bool { flags & 2 != 0 }
        var isSoloable: Bool { flags & 4 != 0 }
        var isAssignable: Bool { flags & 8 != 0 }
    }

    struct Crosspoint: Identifiable, Equatable, Sendable {
        let id: UInt32
        let sourceStripID: UInt32
        let destinationBus: Bus
        let controlID: UInt32
        let enabledMask: UInt32
    }

    let endpointID: AudioEndpointID
    let deviceKind: UInt32
    let topologyRevision: UInt64
    let strips: [Strip]
    let crosspoints: [Crosspoint]
}
