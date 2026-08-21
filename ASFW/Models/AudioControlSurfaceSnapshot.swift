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

    func value(for control: MAudio1814ControlID) -> Int32 {
        value(for: control.rawValue)
    }
}

/// A family of like-typed controls in the 1814's parameter window.
///
/// Mirrors `MAudio1814ControlGroup` in
/// `ASFWDriver/Audio/Protocols/BeBoB/MAudioSpecialParameters.hpp`. The raw values
/// are the driver/app contract — never renumber them.
enum MAudio1814ControlGroup: UInt32, CaseIterable, Sendable {
    case mixerStreamGain = 0x01
    case analogOutputVolume = 0x02
    case mixerAnalogGain = 0x03
    case mixerSpdifGain = 0x04
    case mixerAdatGain = 0x05
    case auxOutputVolume = 0x06
    case headphoneVolume = 0x07
    case mixerAnalogBalance = 0x08
    case mixerSpdifBalance = 0x09
    case mixerAdatBalance = 0x0A
    case auxStreamGain = 0x0B
    case auxAnalogGain = 0x0C
    case auxSpdifGain = 0x0D
    case auxAdatGain = 0x0E
    case physicalMixerSendMask = 0x0F
    case streamMixerSendMask = 0x10
    case headphoneSource = 0x11
    case analogOutputSource = 0x12

    /// How many controls the group contains.
    var count: Int {
        switch self {
        case .mixerStreamGain, .analogOutputVolume, .headphoneVolume,
             .auxStreamGain:
            return 4
        case .mixerAnalogGain, .mixerAdatGain, .mixerAnalogBalance,
             .mixerAdatBalance, .auxAnalogGain, .auxAdatGain:
            return 8
        case .mixerSpdifGain, .mixerSpdifBalance, .auxSpdifGain,
             .auxOutputVolume, .headphoneSource, .analogOutputSource:
            return 2
        case .physicalMixerSendMask, .streamMixerSendMask:
            return 1
        }
    }

    var kind: MAudio1814ControlKind {
        switch self {
        case .mixerAnalogBalance, .mixerSpdifBalance, .mixerAdatBalance:
            return .balance
        case .physicalMixerSendMask, .streamMixerSendMask:
            return .mask
        case .headphoneSource, .analogOutputSource:
            return .selector
        default:
            return .level
        }
    }

    var label: String {
        switch self {
        case .mixerStreamGain: return "Mixer playback gain"
        case .analogOutputVolume: return "Analog output"
        case .mixerAnalogGain: return "Mixer analog in"
        case .mixerSpdifGain: return "Mixer S/PDIF in"
        case .mixerAdatGain: return "Mixer ADAT in"
        case .auxOutputVolume: return "Aux output"
        case .headphoneVolume: return "Headphone"
        case .mixerAnalogBalance: return "Analog in pan"
        case .mixerSpdifBalance: return "S/PDIF in pan"
        case .mixerAdatBalance: return "ADAT in pan"
        case .auxStreamGain: return "Aux playback send"
        case .auxAnalogGain: return "Aux analog send"
        case .auxSpdifGain: return "Aux S/PDIF send"
        case .auxAdatGain: return "Aux ADAT send"
        case .physicalMixerSendMask: return "Physical mixer sends"
        case .streamMixerSendMask: return "Playback mixer sends"
        case .headphoneSource: return "Headphone source"
        case .analogOutputSource: return "Analog output source"
        }
    }
}

enum MAudio1814ControlKind: Sendable {
    /// Attenuation, `levelMin ... levelMax`, where `levelMax` is unity.
    case level
    /// Pan, centred at zero over the full signed range.
    case balance
    /// A bitmask packed into one register.
    case mask
    /// A small enumeration.
    case selector
}

/// One addressable control, `(group << 8) | index`.
struct MAudio1814ControlID: Hashable, Sendable {
    let group: MAudio1814ControlGroup
    let index: UInt32

    init(_ group: MAudio1814ControlGroup, _ index: UInt32 = 0) {
        self.group = group
        self.index = index
    }

    init?(rawValue: UInt32) {
        guard let group = MAudio1814ControlGroup(rawValue: rawValue >> 8) else { return nil }
        let index = rawValue & 0xFF
        guard index < UInt32(group.count) else { return nil }
        self.init(group, index)
    }

    var rawValue: UInt32 { (group.rawValue << 8) | (index & 0xFF) }

    var label: String {
        switch group.kind {
        case .mask:
            return group.label
        default:
            return group.count > 1 ? "\(group.label) \(index + 1)" : group.label
        }
    }

    /// Every control the 1814 exposes, in register order.
    static var all: [MAudio1814ControlID] {
        MAudio1814ControlGroup.allCases.flatMap { group in
            (0..<UInt32(group.count)).map { MAudio1814ControlID(group, $0) }
        }
    }
}

extension MAudio1814ControlID {
    /// Attenuation runs from fully cut to unity; zero is 0 dB, matching the
    /// device's own convention.
    static let levelMin: Int32 = -32768
    static let levelMax: Int32 = 0
    static let balanceMin: Int32 = -32768
    static let balanceMax: Int32 = 32767

    /// The vendor's front-panel step: one knob detent.
    static let levelStep: Int32 = 0x400

    var valueRange: ClosedRange<Int32> {
        switch group.kind {
        case .level: return Self.levelMin...Self.levelMax
        case .balance: return Self.balanceMin...Self.balanceMax
        case .selector: return group == .headphoneSource ? 0...2 : 0...1
        case .mask:
            return group == .physicalMixerSendMask ? 0...0x0003_FFFF : 0...0x0F
        }
    }
}

/// Source of a headphone pair. Wire encoding — never renumber.
enum MAudio1814HeadphoneSource: Int32, CaseIterable, Sendable {
    case mixer12 = 0
    case mixer34 = 1
    case aux = 2

    var label: String {
        switch self {
        case .mixer12: return "Mixer 1"
        case .mixer34: return "Mixer 2"
        case .aux: return "Aux"
        }
    }
}

/// Source of an analog output pair. Wire encoding — never renumber.
enum MAudio1814OutputSource: Int32, CaseIterable, Sendable {
    case mixer = 0
    case aux = 1

    var label: String {
        switch self {
        case .mixer: return "Mixer"
        case .aux: return "Aux"
        }
    }
}
