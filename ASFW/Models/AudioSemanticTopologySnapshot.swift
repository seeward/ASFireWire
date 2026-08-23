import Foundation

/// A bounded, protocol-neutral signal graph published by selector 1028.
/// Control IDs in a matching `AudioControlSurfaceSnapshot` are parameter IDs
/// from this graph, not vendor register addresses.
nonisolated struct AudioSemanticTopologySnapshot: Equatable, Sendable {
    enum NodeKind: UInt32, Sendable { case endpoint = 1, router = 2, mixer = 3, processor = 4 }
    enum EndpointKind: UInt32, Sendable { case none = 0, physical = 1, host = 2 }
    enum PortDirection: UInt32, Sendable { case input = 1, output = 2 }
    enum SignalKind: UInt32, Sendable {
        case none = 0, analogMicXlr = 1, analogInstrument = 2, analogLine = 3, headphone = 4, hostStream = 5
        case digitalSpdif = 6, digitalAdat = 7, auxiliary = 8
    }
    enum TargetKind: UInt32, Sendable { case port = 1, crosspoint = 2, device = 3 }
    enum ParameterKind: UInt32, Sendable {
        case level = 1, mute = 2, phantomPower = 3, phaseInvert = 4, nominalLevel = 5
        case source = 6, stereoLink = 7, hardwareControlTarget = 8, muteFollow = 9
    }
    enum ValueKind: UInt32, Sendable { case boolean = 1, scalar = 2, enumeration = 3 }
    enum Unit: UInt32, Sendable { case none = 0, decibels = 1, normalized = 2 }
    enum Presentation: UInt32, Sendable { case toggle = 1, fader = 2, selector = 3 }
    enum CrosspointPresentation: UInt32, Sendable { case none = 0, primaryFader = 1, routingFader = 2 }
    enum CrosspointGroup: UInt32, Sendable { case none = 0, inputMonitor = 1, hostPlayback = 2 }
    enum MeterKind: UInt32, Sendable { case level = 1, peak = 2 }
    enum MeterUnit: UInt32, Sendable { case native = 0, decibels = 1 }

    struct Node: Identifiable, Equatable, Sendable {
        let id: UInt32
        let kind: NodeKind
        let endpointKind: EndpointKind
    }

    struct Port: Identifiable, Equatable, Sendable {
        let id: UInt32
        let ownerNodeID: UInt32
        let direction: PortDirection
        let signalKind: SignalKind
        let signalIndex: UInt32
    }

    struct FixedLink: Equatable, Sendable {
        let sourcePortID: UInt32
        let destinationPortID: UInt32
    }

    struct Router: Identifiable, Equatable, Sendable {
        let nodeID: UInt32
        let maxActiveBundles: UInt32
        let maxSourcesPerOutput: UInt32
        let maxDestinationsPerInput: UInt32

        var id: UInt32 { nodeID }
    }

    struct RouteBundle: Identifiable, Equatable, Sendable {
        let routerNodeID: UInt32
        let bundleID: UInt32
        let routeOffset: UInt32
        let routeCount: UInt32

        var id: UInt64 { UInt64(routerNodeID) << 32 | UInt64(bundleID) }
    }

    struct Route: Equatable, Sendable {
        let sourcePortID: UInt32
        let destinationPortID: UInt32
    }

    struct Crosspoint: Identifiable, Equatable, Sendable {
        let id: UInt32
        let sourcePortID: UInt32
        let destinationPortID: UInt32
        let presentation: CrosspointPresentation
        let presentationGroup: CrosspointGroup
        let presentationOrder: UInt32
    }

    struct Parameter: Identifiable, Equatable, Sendable {
        let id: UInt32
        let targetKind: TargetKind
        let targetID: UInt32
        let kind: ParameterKind
        let valueKind: ValueKind
        let unit: Unit
        let minimum: Int32
        let maximum: Int32
        let step: Int32
        let presentation: Presentation
    }

    struct Meter: Identifiable, Equatable, Sendable {
        let id: UInt32
        let targetPortID: UInt32
        let kind: MeterKind
        let unit: MeterUnit
        let minimum: Int32
        let maximum: Int32
    }

    let endpointID: AudioEndpointID
    let deviceKind: UInt32
    let topologyRevision: UInt64
    let nodes: [Node]
    let ports: [Port]
    let fixedLinks: [FixedLink]
    let routers: [Router]
    let routeBundles: [RouteBundle]
    let routes: [Route]
    let crosspoints: [Crosspoint]
    let parameters: [Parameter]
    let meters: [Meter]

    func parameter(id: UInt32) -> Parameter? {
        parameters.first(where: { $0.id == id })
    }

    func crosspoint(id: UInt32) -> Crosspoint? {
        crosspoints.first(where: { $0.id == id })
    }
}
