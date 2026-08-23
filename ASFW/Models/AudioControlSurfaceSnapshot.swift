import Foundation

/// One confirmed semantic value from the driver-owned control surface.
/// `id` is opaque to the app and is interpreted only through the matching
/// semantic topology or console-layout snapshot.
struct AudioControlSurfaceValue: Equatable, Sendable {
    let id: UInt32
    let value: Int32
}

/// The family discriminator tells the app which semantic projection can use a
/// surface. It never exposes a register map or a vendor command encoding.
enum AudioControlSurfaceKind: UInt32, Sendable {
    case mAudioSpecialMixer = 0x4D41_3134 // "MA14"
    case apogeeDuet = 0x4455_4554 // "DUET"
}

struct AudioControlSurfaceSnapshot: Equatable, Sendable {
    let endpointID: AudioEndpointID
    let kind: AudioControlSurfaceKind
    let topologyRevision: UInt64
    let stateRevision: UInt32
    let values: [AudioControlSurfaceValue]

    func value(for id: UInt32) -> Int32 {
        values.first(where: { $0.id == id })?.value ?? 0
    }

    nonisolated var isMAudioSpecialMixer: Bool { kind == .mAudioSpecialMixer }
    nonisolated var isSemanticTopologyBacked: Bool { kind == .apogeeDuet }
}
