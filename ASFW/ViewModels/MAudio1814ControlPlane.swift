import Combine
import Foundation
import IOKit

struct MAudio1814ControlPlaneSnapshot {
    let configuration: AudioConfigurationSnapshot
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
    private var cachedConfiguration: AudioConfigurationSnapshot?
    private var nextConfigurationRefresh = Date.distantPast
    private var pending: [MAudio1814ControlID: Int32] = [:]
    private var inFlightRequestID: UInt64?
    private var nextRequestID: UInt64 = 1
    private var lastDispatch = Date.distantPast
    private var configurationRequestToken: UInt64 = 0

    private let minimumWriteSpacing: TimeInterval = 0.050
    private let writeTimeout: TimeInterval = 2.0
    private let configurationRefreshInterval: TimeInterval = 0.5
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
        refresh()
        pollingTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(50))
                guard !Task.isCancelled else { return }
                self?.refresh()
            }
        }
    }

    func stop() {
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
        nextConfigurationRefresh = .distantPast
        isWriteInFlight = false
    }

    func submit(control: MAudio1814ControlID, value: Int32) {
        pending[control] = value // latest intent wins for each semantic control.
        schedulePump()
    }

    func setMeteringEnabled(_ enabled: Bool) {
        guard let endpointID = latest?.configuration.endpointID else { return }
        connector.setAudioMeteringAsync(endpointID: endpointID, enabled: enabled) { [weak self] result in
            Task { @MainActor in
                guard let self else { return }
                self.status = result == KERN_SUCCESS
                    ? (enabled ? "Metering enabled." : "Metering disabled.")
                    : "Metering request rejected: \(self.connector.interpretIOReturn(result))"
                self.refresh()
            }
        }
    }

    private func refresh() {
        guard !refreshInFlight else { return }
        refreshInFlight = true
        if let cachedConfiguration, Date() < nextConfigurationRefresh {
            refreshLiveState(configuration: cachedConfiguration)
            return
        }
        configurationRequestToken &+= 1
        let requestToken = configurationRequestToken
        configurationSnapshotTimeoutTask?.cancel()
        configurationSnapshotTimeoutTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(self?.configurationSnapshotTimeout ?? 2))
            guard !Task.isCancelled, let self,
                  self.refreshInFlight,
                  self.configurationRequestToken == requestToken else { return }
            self.refreshInFlight = false
            self.cachedConfiguration = nil
            self.latest = nil
            self.status = "1814 configuration snapshot timed out; inspect [ControlPlane] logs."
        }
        connector.requestAudioConfigurationSnapshotAsync { [weak self] configuration, result in
            guard let self else { return }
            guard self.configurationRequestToken == requestToken else { return }
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
            self.refreshLiveState(configuration: configuration)
        }
    }

    private func refreshLiveState(configuration: AudioConfigurationSnapshot) {
        var controls: AudioControlSurfaceSnapshot?
        var meters: AudioMeterSnapshot?
        var repliesRemaining = 2
        func receivedReply() {
            repliesRemaining -= 1
            guard repliesRemaining == 0 else { return }
            self.refreshInFlight = false
            guard let controls else {
                self.latest = nil
                self.status = "FireWire 1814 control surface is not ready."
                return
            }
            let snapshot = MAudio1814ControlPlaneSnapshot(
                configuration: configuration, controls: controls, meters: meters)
            self.latest = snapshot
            if !self.isWriteInFlight, self.pending.isEmpty {
                self.status = "Confirmed: \(configuration.committed.sampleRateHz.formatted()) Hz · \(configuration.committed.inputChannels) in / \(configuration.committed.outputChannels) out"
            }
        }
        connector.requestAudioControlSurfaceSnapshotAsync(endpointID: configuration.endpointID) {
            controls = $0
            receivedReply()
        }
        connector.requestAudioMeterSnapshotAsync(endpointID: configuration.endpointID) {
            meters = $0
            receivedReply()
        }
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
        guard !isWriteInFlight,
              let endpointID = latest?.configuration.endpointID,
              let control = pending.keys.sorted(by: { $0.rawValue < $1.rawValue }).first,
              let value = pending.removeValue(forKey: control) else {
            return
        }

        let requestID = nextRequestID
        nextRequestID &+= 1
        inFlightRequestID = requestID
        isWriteInFlight = true
        lastDispatch = Date()
        status = "Applying \(control.label)…"
        connector.submitAudioControlValue(
            endpointID: endpointID, controlID: control, value: value) { [weak self] result in
                Task { @MainActor in self?.complete(requestID: requestID, result: result) }
            }
        watchdogTask?.cancel()
        watchdogTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(self?.writeTimeout ?? 2))
            guard !Task.isCancelled else { return }
            self?.timeout(requestID: requestID)
        }
    }

    private func complete(requestID: UInt64, result: kern_return_t) {
        guard inFlightRequestID == requestID else { return }
        watchdogTask?.cancel()
        watchdogTask = nil
        inFlightRequestID = nil
        isWriteInFlight = false
        status = result == KERN_SUCCESS
            ? "Hardware write confirmed."
            : "Hardware write failed: \(connector.interpretIOReturn(result))"
        refresh()
        schedulePump()
    }

    private func timeout(requestID: UInt64) {
        guard inFlightRequestID == requestID else { return }
        inFlightRequestID = nil
        isWriteInFlight = false
        status = "Hardware write timed out; later intent remains queued."
        refresh()
        schedulePump()
    }
}

private extension MAudio1814ControlID {
    var label: String {
        switch self {
        case .analogOutput12Level: return "analog output 1/2 level"
        case .analogOutput34Level: return "analog output 3/4 level"
        case .headphone12Level: return "headphone 1/2 level"
        case .headphone34Level: return "headphone 3/4 level"
        case .analogOutput12Source: return "analog output 1/2 source"
        case .analogOutput34Source: return "analog output 3/4 source"
        case .headphone12Source: return "headphone 1/2 source"
        case .headphone34Source: return "headphone 3/4 source"
        case .physicalMixerSendMask: return "physical mixer sends"
        case .streamMixerSendMask: return "stream mixer sends"
        }
    }
}
