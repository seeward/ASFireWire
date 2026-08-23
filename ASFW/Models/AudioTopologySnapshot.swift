import Foundation

/// Device-neutral projection consumed by the shared console components. The
/// driver owns strip identity, routes, control bindings, and meter placement;
/// this adapter only turns the semantic layout into view-friendly arrays.
struct AudioTopologySnapshot {
    let topologyRevision: UInt64
    let strips: [AudioTopologyStrip]
    let routes: [AudioTopologyRoute]
}

enum AudioTopologyStripKind {
    case physicalInput
    case playback
    case output
    case aux
    case headphone

    var isInput: Bool { self == .physicalInput || self == .playback }
}

struct AudioTopologyStrip: Identifiable {
    let id: String
    let name: String
    let kind: AudioTopologyStripKind
    let channels: [AudioTopologyStripChannel]
    let sends: [AudioTopologySend]
    let source: AudioTopologyRoute?
}

struct AudioTopologyStripChannel: Identifiable {
    let id: String
    let label: String
    let levelControl: UInt32
    let levelRaw: Int32
    let panControl: UInt32?
    let panRaw: Int32
    let auxControl: UInt32?
    let auxRaw: Int32
    let meterIndex: Int?
}

struct AudioTopologySend: Identifiable {
    let id: String
    let label: String
    let control: UInt32
    let mask: UInt32
    let isEnabled: Bool
}

struct AudioTopologyRoute: Identifiable {
    let id: String
    let name: String
    let control: UInt32
    let choices: [AudioTopologyRouteChoice]
    let selectedValue: Int32
}

struct AudioTopologyRouteChoice: Identifiable {
    let value: Int32
    let name: String
    var id: Int32 { value }
}

enum MAudio1814TopologyProjector {
    static func make(layout: AudioSemanticConsoleLayoutSnapshot,
                     controls: AudioControlSurfaceSnapshot) -> AudioTopologySnapshot? {
        guard layout.endpointID == controls.endpointID,
              layout.topologyRevision == controls.topologyRevision,
              controls.isMAudioSpecialMixer else { return nil }

        let strips = layout.strips.compactMap { strip($0, layout: layout, controls: controls) }
        guard strips.count == layout.strips.count else { return nil }
        return .init(topologyRevision: layout.topologyRevision, strips: strips,
                     routes: strips.compactMap(\.source))
    }

    private static func strip(_ definition: AudioSemanticConsoleLayoutSnapshot.Strip,
                              layout: AudioSemanticConsoleLayoutSnapshot,
                              controls: AudioControlSurfaceSnapshot) -> AudioTopologyStrip? {
        guard let kind = kind(definition.kind), let name = name(definition) else { return nil }
        let channelCount = Int(definition.channelCount)
        let meterCount = Int(definition.meterCount)
        let channels = (0..<channelCount).map { index in
            let level = definition.levelControlID + UInt32(index)
            let pan = definition.panControlID == 0 ? nil : definition.panControlID + UInt32(index)
            let aux = definition.auxControlID == 0 ? nil : definition.auxControlID + UInt32(index)
            return AudioTopologyStripChannel(
                id: "\(definition.id)-\(index)", label: channelCount == 2 ? (index == 0 ? "L" : "R") : "\(index + 1)",
                levelControl: level, levelRaw: controls.value(for: level),
                panControl: pan, panRaw: pan.map { controls.value(for: $0) } ?? 0,
                auxControl: aux, auxRaw: aux.map { controls.value(for: $0) } ?? 0,
                meterIndex: index < meterCount ? Int(definition.meterFirstIndex) + index : nil)
        }
        let sends = layout.crosspoints.filter { $0.sourceStripID == definition.id }
            .sorted { $0.destinationBus.rawValue < $1.destinationBus.rawValue }
            .map { crosspoint in
                let current = UInt32(bitPattern: controls.value(for: crosspoint.controlID))
                return AudioTopologySend(id: "\(definition.id)-\(crosspoint.destinationBus.rawValue)",
                                         label: crosspoint.destinationBus == .main12 ? "1/2" : "3/4",
                                         control: crosspoint.controlID, mask: crosspoint.enabledMask,
                                         isEnabled: current & crosspoint.enabledMask != 0)
            }
        let route = route(definition, controls: controls)
        return .init(id: "strip-\(definition.id)", name: name, kind: kind,
                     channels: channels, sends: sends, source: route)
    }

    private static func kind(_ value: AudioSemanticConsoleLayoutSnapshot.StripKind) -> AudioTopologyStripKind? {
        switch value {
        case .input: .physicalInput
        case .playback: .playback
        case .output: .output
        case .auxiliary: .aux
        case .headphone: .headphone
        }
    }

    private static func name(_ strip: AudioSemanticConsoleLayoutSnapshot.Strip) -> String? {
        let first = strip.firstSignalIndex
        let pair = "\(first)/\(first + UInt32(strip.channelCount) - 1)"
        switch strip.kind {
        case .input:
            switch strip.signalKind {
            case .analogLine: return "ANALOG \(pair) IN"
            case .digitalAdat: return "ADAT \(pair) IN"
            case .digitalSpdif: return "S/PDIF IN"
            default: return nil
            }
        case .playback: return "\(pair) SW RTN"
        case .output: return "\(pair) OUT"
        case .auxiliary: return "AUX"
        case .headphone: return "PHONES \((first + 1) / 2)"
        }
    }

    private static func route(_ strip: AudioSemanticConsoleLayoutSnapshot.Strip,
                              controls: AudioControlSurfaceSnapshot) -> AudioTopologyRoute? {
        guard strip.sourceControlID != 0 else { return nil }
        let choices: [String]
        let name: String
        switch strip.sourceKind {
        case .mixerOrAux:
            name = "Source"
            choices = ["Mixer", "Aux"]
        case .mixer12Mixer34OrAux:
            name = "Mon"
            choices = ["1/2", "3/4", "Aux"]
        case .none:
            return nil
        }
        return .init(id: "route-\(strip.id)", name: name, control: strip.sourceControlID,
                     choices: choices.enumerated().map { .init(value: Int32($0.offset), name: $0.element) },
                     selectedValue: controls.value(for: strip.sourceControlID))
    }
}
