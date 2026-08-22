import Combine
import Darwin.Mach
import Foundation

@MainActor
final class DuetConfigurationViewModel: ObservableObject {
    @Published private(set) var topology: AudioSemanticTopologySnapshot?
    @Published private(set) var controls: AudioControlSurfaceSnapshot?
    @Published private(set) var meters: AudioMeterSnapshot?
    @Published private(set) var configuration: AudioConfigurationSnapshot?
    @Published var selectedRateHz: UInt32 = 48_000
    @Published private(set) var isApplyingConfiguration = false
    @Published private(set) var metersEnabled = true
    @Published private(set) var statusText = "Looking for a Duet published by the audio driver…"
    @Published private(set) var writingParameterIDs: Set<UInt32> = []

    private let connector: ASFWDriverConnector
    private var telemetryEndpointID: AudioEndpointID?
    private var refreshTicks = 0
    private var liveControlRefreshInFlight = false
    /// A successful UserClient request only means the ADK configuration window
    /// was accepted.  Keep the user's selected rate while the later hardware
    /// and Core Audio commit is in flight; otherwise the next snapshot still
    /// reports the old committed rate and makes 44.1 appear to snap to 48.
    private var pendingRateHz: UInt32?
    private var configurationTimeoutTask: Task<Void, Never>?

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    func poll() async {
        defer { deactivateTelemetry() }
        while !Task.isCancelled {
            refreshTicks += 1
            // Meter snapshots need a display-rate pull. Configuration and
            // controls change far less often, so retain their ~1 Hz refresh.
            if refreshTicks.isMultiple(of: 33) || topology == nil { refresh() }
            refreshMeters()
            refreshLiveControls()
            try? await Task.sleep(for: .milliseconds(30))
        }
    }

    func refresh() {
        guard connector.isConnected else {
            topology = nil
            controls = nil
            meters = nil
            configuration = nil
            pendingRateHz = nil
            configurationTimeoutTask?.cancel()
            configurationTimeoutTask = nil
            deactivateTelemetry()
            statusText = "Connect to ASFWDriver to configure a Duet."
            return
        }

        let endpointIDs = connector.getAudioSemanticTopologyEndpointIDs()
        for endpointID in endpointIDs {
            guard let candidateTopology = connector.getAudioSemanticTopology(endpointID: endpointID) else {
                continue
            }
            guard let candidateControls = connector.getAudioControlSurface(endpointID: endpointID),
                  candidateControls.kind == .apogeeDuet else {
                continue
            }
            guard candidateControls.isSemanticTopologyBacked,
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
            configuration = connector.getAudioConfiguration(endpointID: endpointID)
            reconcileConfigurationSelection()
            activateTelemetry(endpointID)
            statusText = "Driver-owned Duet controls are ready."
            return
        }

        topology = nil
        controls = nil
        meters = nil
        configuration = nil
        pendingRateHz = nil
        configurationTimeoutTask?.cancel()
        configurationTimeoutTask = nil
        deactivateTelemetry()
        statusText = "No audio endpoint exposes a Duet semantic topology."
    }

    var console: DuetConsoleSnapshot? {
        guard let topology, let controls else { return nil }
        return DuetConsoleProjector.make(topology: topology, controls: controls, meters: meters)
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

    func setMeteringEnabled(_ enabled: Bool) {
        metersEnabled = enabled
        guard let endpointID = telemetryEndpointID else { return }
        connector.setAudioMeteringAsync(endpointID: endpointID, enabled: enabled) { [weak self] result in
            guard let self, result != KERN_SUCCESS else { return }
            self.statusText = "Could not change Duet telemetry: \(self.connector.interpretIOReturn(result))"
        }
        if !enabled { meters = nil }
    }

    func applySampleRate() {
        guard let configuration, selectedRateHz == 44_100 || selectedRateHz == 48_000 else {
            statusText = "Duet supports 44.1 or 48 kHz in this console."
            return
        }
        guard selectedRateHz != configuration.committed.sampleRateHz else { return }
        isApplyingConfiguration = true
        pendingRateHz = selectedRateHz
        configurationTimeoutTask?.cancel()
        connector.requestAudioConfigurationAsync(endpointID: configuration.endpointID,
                                                 sampleRateHz: selectedRateHz,
                                                 inputOptical: .none, outputOptical: .none) { [weak self] result in
            guard let self else { return }
            guard result == KERN_SUCCESS else {
                self.isApplyingConfiguration = false
                self.pendingRateHz = nil
                self.statusText = "Sample-rate request rejected: \(self.connector.interpretIOReturn(result))"
                self.reconcileConfigurationSelection()
                return
            }
            self.statusText = "Changing sample rate; waiting for Duet and Core Audio to commit it."
            let pendingRate = self.selectedRateHz
            self.configurationTimeoutTask = Task { [weak self] in
                try? await Task.sleep(for: .seconds(8))
                guard !Task.isCancelled, let self,
                      self.pendingRateHz == pendingRate else { return }
                self.pendingRateHz = nil
                self.isApplyingConfiguration = false
                self.reconcileConfigurationSelection()
                self.statusText = "Sample-rate change did not commit; the Duet remains at \(self.configuration?.committed.sampleRateHz ?? 0) Hz."
            }
        }
    }

    private func reconcileConfigurationSelection() {
        guard let configuration else { return }
        guard let pendingRateHz else {
            selectedRateHz = configuration.committed.sampleRateHz
            return
        }
        guard configuration.committed.sampleRateHz == pendingRateHz else { return }
        self.pendingRateHz = nil
        configurationTimeoutTask?.cancel()
        configurationTimeoutTask = nil
        isApplyingConfiguration = false
        selectedRateHz = configuration.committed.sampleRateHz
        statusText = "Sample rate applied: \(configuration.committed.sampleRateHz) Hz."
    }

    private func refreshMeters() {
        guard metersEnabled, let endpointID = telemetryEndpointID,
              let topology, let candidate = connector.getAudioMeterSnapshot(endpointID: endpointID),
              candidate.topologyRevision == topology.topologyRevision else { return }
        // The view model may poll before the next driver sample arrives. Avoid
        // needlessly invalidating the complete console for an unchanged meter
        // revision.
        guard candidate.telemetrySequence != meters?.telemetrySequence else { return }
        meters = candidate
    }

    /// The Duet's physical encoder updates the driver's control surface, not
    /// its meter snapshot. Pull only that small mutable snapshot at display
    /// rate; topology discovery/configuration remain deliberately slow.
    private func refreshLiveControls() {
        guard !liveControlRefreshInFlight,
              let endpointID = telemetryEndpointID,
              let topology else { return }

        liveControlRefreshInFlight = true
        connector.requestAudioControlSurfaceSnapshotAsync(endpointID: endpointID) { [weak self] candidate in
            guard let self else { return }
            self.liveControlRefreshInFlight = false
            guard let candidate,
                  candidate.kind == .apogeeDuet,
                  candidate.endpointID == endpointID,
                  candidate.topologyRevision == topology.topologyRevision,
                  candidate.stateRevision != self.controls?.stateRevision else { return }
            self.controls = candidate
        }
    }

    private func activateTelemetry(_ endpointID: AudioEndpointID) {
        guard telemetryEndpointID != endpointID else { return }
        let previous = telemetryEndpointID
        telemetryEndpointID = endpointID
        if let previous { _ = connector.setAudioMetering(endpointID: previous, enabled: false) }
        connector.setAudioMeteringAsync(endpointID: endpointID, enabled: metersEnabled) { _ in }
    }

    private func deactivateTelemetry() {
        guard let endpointID = telemetryEndpointID else { return }
        telemetryEndpointID = nil
        _ = connector.setAudioMetering(endpointID: endpointID, enabled: false)
    }

}
