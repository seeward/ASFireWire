import Foundation

struct AudioControlSurfaceValue: Equatable, Sendable {
    let id: UInt32
    let value: Int32
}

struct AudioControlSurfaceSnapshot: Equatable, Sendable {
    let endpointID: AudioEndpointID
    let kind: UInt32
    let revision: UInt32
    let values: [AudioControlSurfaceValue]

    func value(for id: UInt32) -> Int32 {
        values.first(where: { $0.id == id })?.value ?? 0
    }
}

enum MAudio1814ControlID: UInt32 {
    case analogOutput12Level = 1
    case analogOutput34Level = 2
    case headphone12Level = 3
    case headphone34Level = 4
    case analogOutput12Source = 5
    case analogOutput34Source = 6
    case headphone12Source = 7
    case headphone34Source = 8
    case physicalMixerSendMask = 9
    case streamMixerSendMask = 10
}
