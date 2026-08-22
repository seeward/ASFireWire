import Combine
import Darwin.Mach
import Foundation

@MainActor
final class DuetConfigurationViewModel: ObservableObject {
    @Published private(set) var topology: AudioSemanticTopologySnapshot?
    @Published private(set) var controls: AudioControlSurfaceSnapshot?
    @Published private(set) var statusText = "Looking for a Duet published by the audio driver…"
    @Published private(set) var writingParameterIDs: Set<UInt32> = []

    private let connector: ASFWDriverConnector

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    func poll() async {
        refresh()
        while !Task.isCancelled {
            try? await Task.sleep(for: .seconds(1))
            guard !Task.isCancelled else { break }
            refresh()
        }
    }

    func refresh() {
        guard connector.isConnected else {
            topology = nil
            controls = nil
            statusText = "Connect to ASFWDriver to configure a Duet."
            return
        }

        let endpointIDs = connector.getAudioConfigurationEndpointIDs()
        for endpointID in endpointIDs {
            guard let candidateTopology = connector.getAudioSemanticTopology(endpointID: endpointID) else {
                continue
            }
            guard let candidateControls = connector.getAudioControlSurface(endpointID: endpointID),
                  candidateControls.isSemanticTopologyBacked,
                  candidateControls.endpointID == candidateTopology.endpointID,
                  candidateControls.topologyRevision == candidateTopology.topologyRevision,
                  Set(candidateControls.values.map(\.id)).isSuperset(of: candidateTopology.parameters.map(\.id)) else {
                topology = candidateTopology
                controls = nil
                statusText = "Waiting for the driver to synchronize Duet controls…"
                return
            }
            topology = candidateTopology
            controls = candidateControls
            statusText = "Driver-owned Duet controls are ready."
            return
        }

        topology = nil
        controls = nil
        statusText = "No audio endpoint exposes a Duet semantic topology."
    }

    func value(for parameter: AudioSemanticTopologySnapshot.Parameter) -> Int32 {
        controls?.value(for: parameter.id) ?? parameter.minimum
    }

    func submit(_ parameter: AudioSemanticTopologySnapshot.Parameter, value: Int32) {
        guard let topology, let controls,
              controls.endpointID == topology.endpointID,
              controls.topologyRevision == topology.topologyRevision,
              value >= parameter.minimum, value <= parameter.maximum else {
            statusText = "That control is not ready."
            return
        }

        writingParameterIDs.insert(parameter.id)
        connector.submitAudioControlValue(
            endpointID: topology.endpointID,
            controlID: parameter.id,
            value: value
        ) { [weak self] result in
            guard let self else { return }
            self.writingParameterIDs.remove(parameter.id)
            if result == KERN_SUCCESS {
                self.statusText = "Applied \(self.title(for: parameter))."
                self.refresh()
            } else {
                self.statusText = "Could not apply \(self.title(for: parameter)): \(self.connector.interpretIOReturn(result))"
            }
        }
    }

    var inputParameters: [AudioSemanticTopologySnapshot.Parameter] {
        guard let topology else { return [] }
        return topology.parameters.filter { parameter in
            switch parameter.kind {
            case .phantomPower, .phaseInvert, .nominalLevel:
                parameter.targetKind == .port
            case .level:
                parameter.targetKind == .port && parameter.unit == .decibels && parameter.minimum >= 0
            default:
                false
            }
        }
    }

    var outputParameters: [AudioSemanticTopologySnapshot.Parameter] {
        guard let topology else { return [] }
        return topology.parameters.filter { parameter in
            parameter.targetKind == .port &&
                ((parameter.kind == .level && parameter.unit == .decibels && parameter.minimum < 0) ||
                 parameter.kind == .mute)
        }
    }

    var mixerParameters: [AudioSemanticTopologySnapshot.Parameter] {
        guard let topology else { return [] }
        return topology.parameters.filter { $0.targetKind == .crosspoint }.sorted { lhs, rhs in
            lhs.targetID < rhs.targetID
        }
    }

    var otherParameters: [AudioSemanticTopologySnapshot.Parameter] {
        guard let topology else { return [] }
        let shownIDs = Set(inputParameters.map(\.id) + outputParameters.map(\.id) + mixerParameters.map(\.id))
        return topology.parameters.filter { !shownIDs.contains($0.id) }
    }

    func title(for parameter: AudioSemanticTopologySnapshot.Parameter) -> String {
        switch parameter.targetKind {
        case .crosspoint:
            return mixerTitle(for: parameter)
        case .port:
            return portTitle(for: parameter)
        }
    }

    private func portTitle(for parameter: AudioSemanticTopologySnapshot.Parameter) -> String {
        switch parameter.kind {
        case .level where parameter.unit == .decibels && parameter.minimum < 0:
            return "Output level"
        case .mute:
            return "Output mute"
        case .level:
            return "Input \(ordinal(of: parameter, among: inputParameters)) gain"
        case .phantomPower:
            return "Input \(ordinal(of: parameter, among: inputParameters)) phantom power"
        case .phaseInvert:
            return "Input \(ordinal(of: parameter, among: inputParameters)) phase invert"
        case .nominalLevel:
            return "Input \(ordinal(of: parameter, among: inputParameters)) nominal level"
        }
    }

    private func mixerTitle(for parameter: AudioSemanticTopologySnapshot.Parameter) -> String {
        guard let topology, let crosspoint = topology.crosspoint(id: parameter.targetID) else {
            return "Mixer crosspoint \(parameter.targetID)"
        }
        let sourceIDs = Array(Set(topology.crosspoints.map(\.sourcePortID))).sorted()
        let destinationIDs = Array(Set(topology.crosspoints.map(\.destinationPortID))).sorted()
        let source = (sourceIDs.firstIndex(of: crosspoint.sourcePortID) ?? 0) + 1
        let destination = (destinationIDs.firstIndex(of: crosspoint.destinationPortID) ?? 0) + 1
        return "Mixer source \(source) to output \(destination)"
    }

    private func ordinal(of parameter: AudioSemanticTopologySnapshot.Parameter,
                         among parameters: [AudioSemanticTopologySnapshot.Parameter]) -> Int {
        let matching = parameters.filter { $0.kind == parameter.kind }.sorted { lhs, rhs in
            lhs.targetID < rhs.targetID
        }
        return (matching.firstIndex(where: { $0.id == parameter.id }) ?? 0) + 1
    }
}
