import Foundation

// MARK: - Presentation Hint Models

struct PortGroupModel: Identifiable {
    var id: String { name }
    let name: String
    let portIds: [UInt32]
}

struct BundleGroupModel: Identifiable {
    var id: String { name }
    let name: String
    let bundleIds: [UInt32]
}

struct PresentationGroupModel: Identifiable {
    let id: UInt32
    let name: String
    let kind: ASFWPresGroupKind
    let nodeIds: [UInt32]
    let portIds: [UInt32]
    let parameterIds: [UInt32]
    let meterIds: [UInt32]
}

struct RouterHintModel: Identifiable {
    var id: UInt32 { routerNodeId }
    let routerNodeId: UInt32
    let style: ASFWRouterStyle
    let inputGroups: [PortGroupModel]
    let outputGroups: [PortGroupModel]
    let bundleGroups: [BundleGroupModel]
}

struct MixerHintModel: Identifiable {
    var id: UInt32 { mixerNodeId }
    let mixerNodeId: UInt32
    let style: ASFWMixerStyle
    let inputGroups: [PortGroupModel]
    let outputGroups: [PortGroupModel]
}

struct ParameterHintModel: Identifiable {
    var id: UInt32 { parameterId }
    let parameterId: UInt32
    let placement: ASFWControlPlacement
    let presentation: ASFWControlPresentation
    let section: String
}

struct ChannelModel: Identifiable {
    let id: UInt32
    let name: String
    let portIds: [UInt32]
}

struct BusModel: Identifiable {
    let id: UInt32
    let semantic: ASFWBusSemantic
    let name: String
    let portIds: [UInt32]
}

struct SendControlModel: Identifiable {
    var id: UInt32 { crosspointId }
    let crosspointId: UInt32
    let busName: String
    let busSemantic: ASFWBusSemantic
    let parameter: ParameterModel
    let presentation: ASFWControlPresentation
}

/// A crosspoint that can be switched in or out of a mixer bus. Distinct from a
/// send *level*: on hardware that places gain on the mixer input port, one gain
/// can feed several buses and each has its own enable.
struct SendEnableModel: Identifiable {
    let id: UInt32          // crosspointId
    let busName: String
    let parameter: ParameterModel
    /// The wire bit means "disabled", so the UI shows the inverse.
    var isEnabled: Bool { !parameter.boolValue }
}

struct ChannelStripModel: Identifiable {
    var id: UInt32 { channelId }
    let channelId: UInt32
    let name: String
    let mainSend: SendControlModel?
    let auxSends: [SendControlModel]
    let sendEnables: [SendEnableModel]
    let pan: ParameterModel?
    let mute: ParameterModel?
    let solo: ParameterModel?
    let phase: ParameterModel?
    let phantom: ParameterModel?
    let nominalLevel: ParameterModel?
    let meter: MeterModel?
}

struct OutputMasterStripModel: Identifiable {
    var id: String { name }
    let name: String
    let level: ParameterModel?
    let mute: ParameterModel?
    let dim: ParameterModel?
    let meters: [MeterModel]
}

struct DevicePresentationModel {
    let groups: [PresentationGroupModel]
    let routerHints: [RouterHintModel]
    let mixerHints: [MixerHintModel]
    let parameterHints: [ParameterHintModel]

    func routerHint(for routerNodeId: UInt32) -> RouterHintModel? {
        return routerHints.first { $0.routerNodeId == routerNodeId }
    }

    func mixerHint(for mixerNodeId: UInt32) -> MixerHintModel? {
        return mixerHints.first { $0.mixerNodeId == mixerNodeId }
    }

    func parameterHint(for parameterId: UInt32) -> ParameterHintModel? {
        return parameterHints.first { $0.parameterId == parameterId }
    }

    func groups(ofKind kind: ASFWPresGroupKind) -> [PresentationGroupModel] {
        return groups.filter { $0.kind == kind }
    }
}

// MARK: - Semantic Lab Snapshot Models

struct EnumItemModel: Identifiable {
    let value: Int64
    let name: String
    var id: Int64 { value }
}

struct ParameterModel: Identifiable {
    let id: UInt32
    let name: String
    let kind: ASFWParamKind
    let semantic: ASFWParameterSemantic
    let targetKind: ASFWTargetKind
    let targetId: UInt32
    var scalarValue: Double
    let scalarMin: Double
    let scalarMax: Double
    let scalarStep: Double
    let unit: String
    var boolValue: Bool
    var enumValue: Int64
    let enumItems: [EnumItemModel]
}

struct PortModel: Identifiable {
    let id: UInt32
    let name: String
    let ownerNodeId: UInt32
    let direction: UInt8
    let channels: UInt32
    let signalKind: ASFWSignalKind
    let signalIndex: UInt32
}

struct RouteModel: Identifiable {
    var id: String { "\(inputPortId)->\(outputPortId)" }
    let inputPortId: UInt32
    let outputPortId: UInt32
}

struct RouteBundleModel: Identifiable {
    let id: UInt32
    let routes: [RouteModel]
    /// Resolved by the model, by walking the topology back to a connector or a
    /// bus. Empty when the walk finds neither.
    let sourceLabel: String
}

struct RouterModel: Identifiable {
    let id: UInt32
    let name: String
    let inputPortIds: [UInt32]
    let outputPortIds: [UInt32]
    let legalBundles: [RouteBundleModel]
    var activeBundleIds: Set<UInt32>
}

struct MixerCrosspointModel: Identifiable {
    let id: UInt32
    let inputPortId: UInt32
    let outputPortId: UInt32
}

struct MixerModel: Identifiable {
    let id: UInt32
    let name: String
    let inputPortIds: [UInt32]
    let outputPortIds: [UInt32]
    let crosspoints: [MixerCrosspointModel]
}

struct NodeModel: Identifiable {
    let id: UInt32
    let name: String
    let kind: ASFWNodeKind
    let inputPortIds: [UInt32]
    let outputPortIds: [UInt32]
}

struct MeterModel: Identifiable {
    let id: UInt32
    let name: String
    let targetPortId: UInt32
    let value: Double
    let min: Double
    let max: Double
}

struct LabDeviceSnapshot {
    let revision: UInt64
    let deviceKind: ASFWVirtualDeviceKind
    let manufacturer: String
    let model: String
    let currentSampleRate: UInt32
    let opticalInput: ASFWOpticalMode
    let opticalOutput: ASFWOpticalMode
    let supportedSampleRates: [UInt32]
    let hasOptical: Bool
    let totalCaptureChannels: UInt32
    let totalPlaybackChannels: UInt32
    let linkCount: UInt32

    let nodes: [NodeModel]
    let ports: [PortModel]
    let mixers: [MixerModel]
    let routers: [RouterModel]
    let parameters: [ParameterModel]
    let meters: [MeterModel]
    let channels: [ChannelModel]
    let buses: [BusModel]
    let presentation: DevicePresentationModel

    func portName(for portId: UInt32) -> String {
        if let p = ports.first(where: { $0.id == portId }) {
            return p.name
        }
        return "Port \(portId)"
    }

    func parameter(forId paramId: UInt32) -> ParameterModel? {
        return parameters.first { $0.id == paramId }
    }

    func meter(forId meterId: UInt32) -> MeterModel? {
        return meters.first { $0.id == meterId }
    }

    func parameters(forTargetNode nodeId: UInt32) -> [ParameterModel] {
        return parameters.filter { $0.targetKind == ASFW_TARGET_NODE && $0.targetId == nodeId }
    }

    func parameters(forTargetPort portId: UInt32) -> [ParameterModel] {
        return parameters.filter { $0.targetKind == ASFW_TARGET_PORT && $0.targetId == portId }
    }

    func parameter(forTargetCrosspoint cpId: UInt32) -> ParameterModel? {
        return parameters.first { $0.targetKind == ASFW_TARGET_CROSSPOINT && $0.targetId == cpId }
    }

    func meters(forTargetPort portId: UInt32) -> [MeterModel] {
        return meters.filter { $0.targetPortId == portId }
    }
}

struct LabEventModel: Identifiable {
    let id: UInt64
    let kind: ASFWEventKind
    let accepted: Bool
    let revision: UInt64
    let targetId: UInt32
    let label: String
    let before: String
    let after: String
    let detail: String
    let line: String

    var isConfiguration: Bool {
        kind == ASFW_EVENT_CONFIG_COMMITTED || kind == ASFW_EVENT_CONFIG_REJECTED
    }

    var kindText: String {
        switch kind {
        case ASFW_EVENT_CONFIG_COMMITTED, ASFW_EVENT_CONFIG_REJECTED: return "CONFIG"
        case ASFW_EVENT_PARAMETER_CHANGED, ASFW_EVENT_PARAMETER_REJECTED: return "PARAM"
        default: return "ROUTE"
        }
    }
}
