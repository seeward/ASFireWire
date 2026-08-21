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
    @Published private(set) var console = MAudio1814ConsoleState()
    @Published private(set) var peakHold = AudioMeterPeakHold()

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
                if let topology { console.reconcile(with: topology) }
                // A nil or disabled snapshot means no new samples are arriving.
                // Without this the bars would freeze at their last value and
                // read as live signal that is not there any more.
                if let meters = state.meters, meters.isEnabled {
                    peakHold.observe(meters)
                } else {
                    peakHold.reset()
                }
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
        return MAudio1814TopologyProjector.make(configuration: snapshot, controls: mixerSnapshot)
    }

    // MARK: - Console

    func isLinked(_ strip: AudioTopologyStrip) -> Bool { console.isLinked(strip.id) }
    func isMuted(_ strip: AudioTopologyStrip) -> Bool { console.isMuted(strip.id) }
    func isSoloed(_ strip: AudioTopologyStrip) -> Bool { console.isSoloed(strip.id) }
    var isSoloActive: Bool { console.isSoloActive }

    func isSuppressed(_ strip: AudioTopologyStrip) -> Bool {
        console.isSuppressed(strip.id, kind: strip.kind)
    }

    /// What a fader should show. While a strip is suppressed the device holds
    /// silence, so the confirmed value would drag every fader to the bottom.
    func displayedLevel(_ strip: AudioTopologyStrip,
                        _ channel: AudioTopologyStripChannel) -> Int32 {
        console.displayLevel(channel.levelControl, confirmed: channel.levelRaw,
                             suppressed: isSuppressed(strip))
    }

    /// Moves one channel's fader, or both when the strip is linked.
    func setLevel(_ strip: AudioTopologyStrip, _ channel: AudioTopologyStripChannel,
                  position: Double) {
        let raw = MAudio1814Level.raw(position: position)
        let targets = console.isLinked(strip.id) ? strip.channels : [channel]
        let suppressed = console.isSuppressed(strip.id, kind: strip.kind)
        for target in targets {
            console.setIntendedLevel(target.levelControl, raw)
            if !suppressed {
                applyMixerControl(target.levelControl, value: raw)
            }
        }
    }

    func setPan(_ strip: AudioTopologyStrip, _ channel: AudioTopologyStripChannel,
                position: Double) {
        guard let control = channel.panControl else { return }
        applyMixerControl(control, value: MAudio1814Level.rawPan(position: position))
    }

    /// Aux sends stay per-channel even when the fader pair is linked: the aux
    /// bus is a separate mix and ganging it would remove the point of having it.
    func setAux(_ channel: AudioTopologyStripChannel, position: Double) {
        guard let control = channel.auxControl else { return }
        applyMixerControl(control, value: MAudio1814Level.raw(position: position))
    }

    func toggleLink(_ strip: AudioTopologyStrip) { console.toggleLink(strip.id) }

    func toggleMute(_ strip: AudioTopologyStrip) {
        console.toggleMute(strip.id)
        flushConsoleLevels()
    }

    func toggleSolo(_ strip: AudioTopologyStrip) {
        console.toggleSolo(strip.id)
        flushConsoleLevels()
    }

    /// Writes every level the current mute/solo state implies. Only controls
    /// that actually differ are submitted, so an unmute of one strip does not
    /// re-send the whole console.
    private func flushConsoleLevels() {
        guard let topology else { return }
        for (control, value) in console.pendingLevelWrites(for: topology) {
            applyMixerControl(control, value: value)
        }
    }

    func setTopologySend(_ send: AudioTopologySend, enabled: Bool) {
        guard let mixerSnapshot else { return }
        let current = UInt32(bitPattern: mixerSnapshot.value(for: send.control.rawValue))
        let next = enabled ? current | send.mask : current & ~send.mask
        applyMixerControl(send.control, value: Int32(bitPattern: next))
    }

}
