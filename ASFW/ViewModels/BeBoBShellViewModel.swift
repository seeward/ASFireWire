// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBShellViewModel.swift — ViewModel for the BeBoB Virtual UART & Telemetry Dashboard.

import Foundation
import Combine

final class BeBoBShellViewModel: ObservableObject {
    @Published var isConnected: Bool = false
    @Published var isExecuting: Bool = false
    @Published var terminalOutput: String = "CMDLINE tool ready for commands. Try 'help' for help.\r\n1814> "
    @Published var commandInput: String = ""
    @Published var streamingStats: BeBoBSwiftStreamingStats? = nil
    @Published var avStat: BeBoBSwiftAvStat? = nil
    @Published var syncState: BeBoBSwiftSyncState? = nil
    @Published var isAutoPolling: Bool = false
    @Published var availableDevices: [FWDeviceInfo] = []
    @Published var selectedDeviceID: DeviceInstanceID = DeviceInstanceID(1)
    @Published var errorMessage: String? = nil

    var commandHistory: [String] = []
    var historyIndex: Int = -1
    private var isRefreshingTelemetry = false

    private let connector: ASFWDriverConnector
    private var cancellables = Set<AnyCancellable>()
    private var pollTimer: Timer? = nil

    init(connector: ASFWDriverConnector) {
        self.connector = connector

        connector.$isConnected
            .receive(on: DispatchQueue.main)
            .sink { [weak self] connected in
                guard let self else { return }
                self.isConnected = connected
                if connected {
                    self.refreshDevices()
                }
            }
            .store(in: &cancellables)

        isConnected = connector.isConnected
        if isConnected {
            refreshDevices()
        }
    }

    func refreshDevices() {
        if let devices = connector.getDiscoveredDevices() {
            self.availableDevices = devices
            // If current selectedDeviceID is not in active/ready devices, auto-select the first ready device
            if !devices.contains(where: { $0.id == selectedDeviceID && $0.state == .ready }) {
                if let ready = devices.first(where: { $0.state == .ready }) {
                    self.selectedDeviceID = ready.id
                } else if let first = devices.first {
                    self.selectedDeviceID = first.id
                }
            }
        }
    }

    @discardableResult
    func resolveActiveDeviceID() -> DeviceInstanceID {
        refreshDevices()
        return selectedDeviceID
    }

    deinit {
        stopAutoPolling()
    }

    // MARK: - Shell Actions

    @MainActor
    func sendCommand(_ explicitCmd: String? = nil) {
        let cmd = explicitCmd ?? commandInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cmd.isEmpty else { return }

        let deviceID = resolveActiveDeviceID()

        if explicitCmd == nil {
            commandHistory.append(cmd)
            historyIndex = commandHistory.count
            commandInput = ""
        }

        // The device echoes the command line and prints its own prompt, so
        // synthesising either here splices duplicate text into the middle of
        // the response.
        isExecuting = true
        errorMessage = nil

        Task {
            let output = await connector.executeBeBoBShellCommand(deviceID: deviceID, command: cmd)
            await MainActor.run {
                self.isExecuting = false
                if let output, !output.isEmpty {
                    self.appendToTerminal(output)
                } else {
                    self.appendToTerminal("\(cmd)\r\n(no output or timeout)\r\n")
                }
            }
        }
    }

    @MainActor
    func refreshTelemetry() {
        // Three gated conversations take well over a second. Without this the
        // 1 Hz poll timer queues refreshes faster than the mailbox can serve
        // them and the backlog grows without bound.
        guard !isRefreshingTelemetry else { return }
        isRefreshingTelemetry = true

        let deviceID = resolveActiveDeviceID()
        Task {
            // Sequential on purpose. Each of these is a full mailbox
            // conversation and the DM1000 serves exactly one at a time; issuing
            // them concurrently only queues them behind the gate while making
            // the completion order unpredictable.
            let stats = await connector.fetchBeBoBStreamingStats(deviceID: deviceID)
            let av = await connector.fetchBeBoBAvStat(deviceID: deviceID)
            let sync = await connector.fetchBeBoBSyncState(deviceID: deviceID)

            await MainActor.run {
                self.streamingStats = stats
                self.avStat = av
                self.syncState = sync
                self.isRefreshingTelemetry = false
            }
        }
    }

    @MainActor
    func drainFIFO() {
        let deviceID = resolveActiveDeviceID()
        isExecuting = true
        Task {
            let output = await connector.drainStdoutFIFO(deviceID: deviceID, maxChunks: 32)
            await MainActor.run {
                self.isExecuting = false
                if !output.isEmpty {
                    self.appendToTerminal(output)
                }
            }
        }
    }

    func toggleAutoPolling() {
        if isAutoPolling {
            stopAutoPolling()
        } else {
            startAutoPolling()
        }
    }

    func startAutoPolling() {
        isAutoPolling = true
        pollTimer?.invalidate()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.refreshTelemetry()
            }
        }
    }

    func stopAutoPolling() {
        isAutoPolling = false
        pollTimer?.invalidate()
        pollTimer = nil
    }

    func clearTerminal() {
        terminalOutput = "1814> "
    }

    private func appendToTerminal(_ text: String) {
        terminalOutput += text
        // Keep terminal buffer bounded to last 20,000 characters
        if terminalOutput.count > 20_000 {
            terminalOutput = String(terminalOutput.suffix(15_000))
        }
    }
}
