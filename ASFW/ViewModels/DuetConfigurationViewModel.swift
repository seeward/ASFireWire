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

        let endpointIDs = connector.getAudioSemanticTopologyEndpointIDs()
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

    var console: DuetConsoleSnapshot? {
        guard let topology, let controls else { return nil }
        return DuetConsoleProjector.make(topology: topology, controls: controls)
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
                self.statusText = "Applied Duet control."
                self.refresh()
            } else {
                self.statusText = "Could not apply Duet control: \(self.connector.interpretIOReturn(result))"
            }
        }
    }

}
