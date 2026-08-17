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

        appendToTerminal("\(cmd)\r\n")
        isExecuting = true
        errorMessage = nil

        Task {
            let output = await connector.executeBeBoBShellCommand(deviceID: deviceID, command: cmd)
            await MainActor.run {
                self.isExecuting = false
                if let output, !output.isEmpty {
                    self.appendToTerminal(output)
                } else {
                    self.appendToTerminal("(no output or timeout)\r\n")
                }
                self.appendToTerminal("1814> ")
            }
        }
    }

    @MainActor
    func refreshTelemetry() {
        let deviceID = resolveActiveDeviceID()
        Task {
            async let statsTask = connector.fetchBeBoBStreamingStats(deviceID: deviceID)
            async let avStatTask = connector.fetchBeBoBAvStat(deviceID: deviceID)
            async let syncTask = connector.fetchBeBoBSyncState(deviceID: deviceID)

            let (stats, av, sync) = await (statsTask, avStatTask, syncTask)
            await MainActor.run {
                self.streamingStats = stats
                self.avStat = av
                self.syncState = sync
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
