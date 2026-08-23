import Combine
import Foundation
import IOKit

struct MAudio1814ControlPlaneSnapshot {
    let configuration: AudioConfigurationSnapshot
    let layout: AudioSemanticConsoleLayoutSnapshot
    let controls: AudioControlSurfaceSnapshot
    let meters: AudioMeterSnapshot?
}

/// The only live-control path for the 1814 UI.
///
/// SwiftUI submits intent here; this object coalesces it, sends at most one
/// semantic write every 50 ms, and waits for the driver's asynchronous action
/// completion before issuing another write. The UI is updated solely from a
/// later confirmed-belief snapshot. No view and no MainActor method invokes an
/// `IOConnectCall*` function.
@MainActor
final class MAudio1814ControlPlane: ObservableObject {
    @Published private(set) var latest: MAudio1814ControlPlaneSnapshot?
    @Published private(set) var status = "Connecting to FireWire 1814…"
    @Published private(set) var isWriteInFlight = false

    private let connector: ASFWDriverConnector
    private var pollingTask: Task<Void, Never>?
    private var pumpTask: Task<Void, Never>?
    private var watchdogTask: Task<Void, Never>?
    private var configurationSnapshotTimeoutTask: Task<Void, Never>?
    private var refreshInFlight = false
    private var meterRefreshInFlight = false
    private var controlsRefreshInFlight = false
    private var layoutRefreshInFlight = false
    private var nextControlsRefresh = Date.distantPast
    private var cachedControls: AudioControlSurfaceSnapshot?
    private var cachedMeters: AudioMeterSnapshot?
    private var cachedConfiguration: AudioConfigurationSnapshot?
    private var cachedLayout: AudioSemanticConsoleLayoutSnapshot?
    private var nextConfigurationRefresh = Date.distantPast
    private var pending: [UInt32: Int32] = [:]
    private var inFlightRequestID: UInt64?
    private var nextRequestID: UInt64 = 1
    private var lastDispatch = Date.distantPast
    private var configurationRequestToken: UInt64 = 0
    /// Invalidates every completion from a prior start/stop cycle. DriverKit
    /// async actions cannot be cancelled after submission.
    private var sessionEpoch: UInt64 = 0

    private let minimumWriteSpacing: TimeInterval = 0.050
    private let writeTimeout: TimeInterval = 2.0
    private let configurationRefreshInterval: TimeInterval = 0.5
    private let controlsRefreshInterval: TimeInterval = 0.25
    private let configurationSnapshotTimeout: TimeInterval = 2.0

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    deinit {
        pollingTask?.cancel()
        pumpTask?.cancel()
        watchdogTask?.cancel()
        configurationSnapshotTimeoutTask?.cancel()
    }

    func start() {
        guard pollingTask == nil else { return }
        sessionEpoch &+= 1
        let epoch = sessionEpoch
        pollingTask = Task { [weak self, epoch] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(20))
                guard !Task.isCancelled else { return }
                self?.refresh(epoch: epoch)
            }
        }
        refresh(epoch: epoch)
    }

    func stop() {
        sessionEpoch &+= 1
        configurationRequestToken &+= 1
        pollingTask?.cancel()
        pollingTask = nil
        pumpTask?.cancel()
        pumpTask = nil
        watchdogTask?.cancel()
        watchdogTask = nil
        configurationSnapshotTimeoutTask?.cancel()
        configurationSnapshotTimeoutTask = nil
        pending.removeAll()
        inFlightRequestID = nil
        cachedConfiguration = nil
        cachedLayout = nil
        cachedControls = nil
        cachedMeters = nil
        nextConfigurationRefresh = .distantPast
        nextControlsRefresh = .distantPast
        refreshInFlight = false
        meterRefreshInFlight = false
        controlsRefreshInFlight = false
        layoutRefreshInFlight = false
        isWriteInFlight = false
        latest = nil
    }

    func submit(control: UInt32, value: Int32) {
        pending[control] = value // latest intent wins for each semantic control.
        schedulePump()
    }

    func setMeteringEnabled(_ enabled: Bool) {
        let epoch = sessionEpoch
        guard let endpointID = latest?.configuration.endpointID else { return }
        connector.setAudioMeteringAsync(endpointID: endpointID, enabled: enabled) { [weak self] result in
            Task { @MainActor in
                guard let self, self.sessionEpoch == epoch else { return }
                self.status = result == KERN_SUCCESS
                    ? (enabled ? "Metering enabled." : "Metering disabled.")
                    : "Metering request rejected: \(self.connector.interpretIOReturn(result))"
                self.refresh(epoch: epoch)
            }
        }
    }

    private func refresh(epoch: UInt64) {
        guard sessionEpoch == epoch else { return }
        guard !refreshInFlight else { return }
        refreshInFlight = true
        if let cachedConfiguration, Date() < nextConfigurationRefresh {
            refreshLiveState(configuration: cachedConfiguration, epoch: epoch)
            return
        }
        configurationRequestToken &+= 1
        let requestToken = configurationRequestToken
        configurationSnapshotTimeoutTask?.cancel()
        configurationSnapshotTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(self?.configurationSnapshotTimeout ?? 2))
            guard !Task.isCancelled, let self,
                  self.sessionEpoch == epoch,
                  self.refreshInFlight,
                  self.configurationRequestToken == requestToken else { return }
            self.refreshInFlight = false
            self.cachedConfiguration = nil
            self.latest = nil
            self.status = "1814 configuration snapshot timed out; inspect [ControlPlane] logs."
        }
        connector.requestAudioConfigurationSnapshotAsync { [weak self] configuration, result in
            guard let self else { return }
            guard self.sessionEpoch == epoch,
                  self.configurationRequestToken == requestToken else { return }
            self.configurationSnapshotTimeoutTask?.cancel()
            self.configurationSnapshotTimeoutTask = nil
            guard let configuration else {
                self.refreshInFlight = false
                self.cachedConfiguration = nil
                self.latest = nil
                self.status = "1814 configuration snapshot failed: \(self.connector.interpretIOReturn(result))"
                return
            }
            self.cachedConfiguration = configuration
            self.nextConfigurationRefresh = Date().addingTimeInterval(self.configurationRefreshInterval)
            self.refreshLiveState(configuration: configuration, epoch: epoch)
        }
    }

    /// Meters and the control surface are polled **independently**.
    ///
    /// They used to share one completion gate, so a snapshot was published only
    /// once both had returned. That made the meter rate the slower of the two —
    /// and the control surface is much the slower, because it costs an async
    /// header reply plus a struct read. Metering is the thing that has to look
    /// live; the control surface only changes when we write or somebody turns a
    /// knob, so it polls at a quarter of the rate and never holds meters up.
    private func refreshLiveState(configuration: AudioConfigurationSnapshot, epoch: UInt64) {
        guard sessionEpoch == epoch else { return }
        refreshInFlight = false

        if !layoutRefreshInFlight &&
            (cachedLayout?.endpointID != configuration.endpointID ||
             cachedLayout?.topologyRevision != configuration.topologyRevision) {
            layoutRefreshInFlight = true
            connector.requestAudioSemanticConsoleLayout(endpointID: configuration.endpointID) {
                [weak self] layout in
                guard let self, self.sessionEpoch == epoch else { return }
                self.layoutRefreshInFlight = false
                if let layout,
                   layout.endpointID == configuration.endpointID,
                   layout.topologyRevision == configuration.topologyRevision {
                    self.cachedLayout = layout
                } else {
                    self.cachedLayout = nil
                }
                self.publish(configuration: configuration, epoch: epoch)
            }
        }

        if !meterRefreshInFlight {
            meterRefreshInFlight = true
            connector.requestAudioMeterSnapshotAsync(endpointID: configuration.endpointID) {
                [weak self] meters in
                guard let self, self.sessionEpoch == epoch else { return }
                self.meterRefreshInFlight = false
                if let meters,
                   meters.endpointID == configuration.endpointID,
                   meters.topologyRevision == configuration.topologyRevision {
                    self.cachedMeters = meters
                } else {
                    self.cachedMeters = nil
                }
                self.publish(configuration: configuration, epoch: epoch)
            }
        }

        guard !controlsRefreshInFlight, Date() >= nextControlsRefresh else { return }
        controlsRefreshInFlight = true
        connector.requestAudioControlSurfaceSnapshotAsync(endpointID: configuration.endpointID) {
            [weak self] controls in
            guard let self, self.sessionEpoch == epoch else { return }
            self.controlsRefreshInFlight = false
            self.nextControlsRefresh = Date().addingTimeInterval(self.controlsRefreshInterval)
            if let controls,
               controls.endpointID == configuration.endpointID,
               controls.topologyRevision == configuration.topologyRevision {
                self.cachedControls = controls
            } else {
                self.cachedControls = nil
            }
            self.publish(configuration: configuration, epoch: epoch)
        }
    }

    /// Publishes a coherent definition/state pair. Meter telemetry remains
    /// independent, but is attached only when it names the same topology.
    private func publish(configuration: AudioConfigurationSnapshot, epoch: UInt64) {
        guard sessionEpoch == epoch,
              cachedConfiguration?.endpointID == configuration.endpointID,
              cachedConfiguration?.topologyRevision == configuration.topologyRevision,
              let layout = cachedLayout,
              layout.endpointID == configuration.endpointID,
              layout.topologyRevision == configuration.topologyRevision,
              let controls = cachedControls,
              controls.endpointID == configuration.endpointID,
              controls.topologyRevision == configuration.topologyRevision else {
            if !controlsRefreshInFlight {
                self.status = "FireWire 1814 control surface is not ready."
            }
            return
        }
        let meters = cachedMeters.flatMap { meter in
            meter.endpointID == configuration.endpointID &&
            meter.topologyRevision == configuration.topologyRevision ? meter : nil
        }
        latest = MAudio1814ControlPlaneSnapshot(
            configuration: configuration, layout: layout, controls: controls, meters: meters)
        if !isWriteInFlight, pending.isEmpty {
            status = "Confirmed: \(configuration.committed.sampleRateHz.formatted()) Hz · \(configuration.committed.inputChannels) in / \(configuration.committed.outputChannels) out"
        }
    }

    /// Pulls the control surface on the next tick instead of waiting out the
    /// slow cadence — used after a write, when the confirmed value is the whole
    /// point of the next poll.
    private func invalidateControlsCache() {
        nextControlsRefresh = .distantPast
    }

    private func schedulePump() {
        guard !isWriteInFlight, !pending.isEmpty else { return }
        pumpTask?.cancel()
        let delay = max(0, minimumWriteSpacing - Date().timeIntervalSince(lastDispatch))
        pumpTask = Task { [weak self] in
            if delay > 0 { try? await Task.sleep(for: .seconds(delay)) }
            guard !Task.isCancelled else { return }
            self?.dispatchNext()
        }
    }

    private func dispatchNext() {
        let epoch = sessionEpoch
        guard !isWriteInFlight,
              let endpointID = latest?.configuration.endpointID,
              let control = pending.keys.sorted().first,
              let value = pending.removeValue(forKey: control) else {
            return
        }

        let requestID = nextRequestID
        nextRequestID &+= 1
        inFlightRequestID = requestID
        isWriteInFlight = true
        lastDispatch = Date()
        status = "Applying hardware control…"
        connector.submitAudioControlValue(
            endpointID: endpointID, controlID: control, value: value) { [weak self] result in
                Task { @MainActor in
                    self?.complete(epoch: epoch, requestID: requestID, result: result)
                }
            }
        watchdogTask?.cancel()
        watchdogTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(self?.writeTimeout ?? 2))
            guard !Task.isCancelled else { return }
            self?.timeout(epoch: epoch, requestID: requestID)
        }
    }

    private func complete(epoch: UInt64, requestID: UInt64, result: kern_return_t) {
        guard sessionEpoch == epoch, inFlightRequestID == requestID else { return }
        watchdogTask?.cancel()
        watchdogTask = nil
        inFlightRequestID = nil
        isWriteInFlight = false
        status = result == KERN_SUCCESS
            ? "Hardware write confirmed."
            : "Hardware write failed: \(connector.interpretIOReturn(result))"
        invalidateControlsCache()
        refresh(epoch: epoch)
        schedulePump()
    }

    private func timeout(epoch: UInt64, requestID: UInt64) {
        guard sessionEpoch == epoch, inFlightRequestID == requestID else { return }
        inFlightRequestID = nil
        isWriteInFlight = false
        status = "Hardware write timed out; later intent remains queued."
        refresh(epoch: epoch)
        schedulePump()
    }
}
