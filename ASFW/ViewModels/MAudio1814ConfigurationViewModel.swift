import Combine
import Darwin.Mach
import Foundation

@MainActor
final class MAudio1814ConfigurationViewModel: ObservableObject {
    @Published private(set) var snapshot: AudioConfigurationSnapshot?
    @Published private(set) var mixerSnapshot: AudioControlSurfaceSnapshot?
    @Published private(set) var meterSnapshot: AudioMeterSnapshot?
    @Published var selectedRateHz: UInt32 = 48_000
    @Published var selectedInputOptical: AudioOpticalMode = .spdif
    @Published var selectedOutputOptical: AudioOpticalMode = .spdif
    @Published private(set) var statusText = "Looking for a FireWire 1814…"
    @Published private(set) var isApplying = false

    private let connector: ASFWDriverConnector
    let controlPlane: MAudio1814ControlPlane
    private var controlPlaneCancellables = Set<AnyCancellable>()
    private var configurationWatchdog: Task<Void, Never>?

    init(connector: ASFWDriverConnector) {
        self.connector = connector
        controlPlane = MAudio1814ControlPlane(connector: connector)
        controlPlane.$latest
            .compactMap { $0 }
            .sink { [weak self] state in
                guard let self else { return }
                let configurationChanged = snapshot != state.configuration
                snapshot = state.configuration
                mixerSnapshot = state.controls
                meterSnapshot = state.meters
                if configurationChanged {
                    selectedRateHz = state.configuration.committed.sampleRateHz
                    selectedInputOptical = state.configuration.committed.inputOptical
                    selectedOutputOptical = state.configuration.committed.outputOptical
                    isApplying = false
                    configurationWatchdog?.cancel()
                    configurationWatchdog = nil
                }
            }
            .store(in: &controlPlaneCancellables)
        controlPlane.$status
            .sink { [weak self] status in
                guard let self, !self.isApplying else { return }
                self.statusText = status
            }
            .store(in: &controlPlaneCancellables)
    }

    func poll() async {
        controlPlane.start()
        while !Task.isCancelled {
            try? await Task.sleep(for: .seconds(1))
        }
        controlPlane.stop()
    }

    func apply() {
        guard let snapshot else { return }
        guard !(selectedRateHz == snapshot.committed.sampleRateHz &&
                selectedInputOptical == snapshot.committed.inputOptical &&
                selectedOutputOptical == snapshot.committed.outputOptical) else {
            statusText = "That configuration is already committed."
            return
        }
        isApplying = true
        statusText = "Requesting configuration…"
        configurationWatchdog?.cancel()
        configurationWatchdog = Task { [weak self] in
            try? await Task.sleep(for: .seconds(8))
            guard !Task.isCancelled, let self, self.isApplying else { return }
            self.isApplying = false
            self.statusText = "Configuration request timed out before Core Audio committed it."
        }
        connector.requestAudioConfigurationAsync(
            endpointID: snapshot.endpointID,
            sampleRateHz: selectedRateHz,
            inputOptical: selectedInputOptical,
            outputOptical: selectedOutputOptical
        ) { [weak self] result in
            guard let self else { return }
            if result == KERN_SUCCESS {
                self.statusText = "Requested; waiting for Core Audio to commit the new geometry."
            } else {
                self.configurationWatchdog?.cancel()
                self.configurationWatchdog = nil
                self.statusText = "Request rejected: \(self.connector.interpretIOReturn(result))"
                self.isApplying = false
            }
        }
    }

    func applyMixerControl(_ controlID: MAudio1814ControlID, value: Int32) {
        controlPlane.submit(control: controlID, value: value)
    }

    func setMeteringEnabled(_ enabled: Bool) {
        controlPlane.setMeteringEnabled(enabled)
    }

    var supportedRates: [UInt32] {
        Array(Set(snapshot?.capabilities.map(\.sampleRateHz) ?? [])).sorted()
    }

    var topology: AudioTopologySnapshot? {
        guard let snapshot, let mixerSnapshot else { return nil }
        return MAudio1814TopologyProjector.make(
            configuration: snapshot, controls: mixerSnapshot, meters: meterSnapshot)
    }

    func setTopologyLevel(_ control: MAudio1814ControlID, percent: Double) {
        applyMixerControl(control, value: MAudio1814TopologyProjector.rawLevel(percent: percent))
    }

    func setTopologySend(_ send: AudioTopologySend, enabled: Bool) {
        guard let mixerSnapshot else { return }
        let current = UInt32(bitPattern: mixerSnapshot.value(for: send.control.rawValue))
        let next = enabled ? current | send.mask : current & ~send.mask
        applyMixerControl(send.control, value: Int32(bitPattern: next))
    }

}
