// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

/// Minimal line discipline for the BridgeCo shell's echo.
///
/// The shell does not echo a keystroke — it *redraws the whole input line*, as
/// `BS x len(previous)` then the line, then a space, then one more `BS`. Appending
/// those bytes verbatim renders every keystroke as its own fragment, so typing
/// `help` shows up as `h he hel help`. Applying the control characters collapses
/// the redraws back into the single line the device is actually drawing.
///
/// Backspace here moves a cursor and later characters overwrite; it does not
/// delete. Treating `BS` as "drop the last character" mangles the redraw, because
/// the device backs up over the whole line and rewrites it in place.
struct VirtualUartScreen {
    private var committed: String = ""
    private var line: [Character] = []
    private var cursor: Int = 0

    /// Rendered contents: finished lines plus the line currently being drawn.
    var rendered: String { committed + String(line) }

    mutating func reset() {
        committed = ""
        line.removeAll()
        cursor = 0
    }

    mutating func append<S: StringProtocol>(_ text: S) {
        for scalar in text.unicodeScalars {
            switch scalar.value {
            case 0x0D: // CR — back to column zero; what follows overwrites
                cursor = 0
            case 0x0A: // LF — commit the line
                committed += String(line) + "\n"
                line.removeAll(keepingCapacity: true)
                cursor = 0
            case 0x08: // BS — move left, do not delete
                if cursor > 0 { cursor -= 1 }
            case 0x09: // HT — next 8-column stop
                repeat { put(" ") } while cursor % 8 != 0
            default:
                guard scalar.value >= 0x20 else { continue } // drop other controls
                put(Character(scalar))
            }
        }
        trimIfNeeded()
    }

    private mutating func put(_ character: Character) {
        if cursor < line.count {
            line[cursor] = character
        } else {
            line.append(character)
        }
        cursor += 1
    }

    private mutating func trimIfNeeded() {
        guard committed.count > 20_000 else { return }
        committed = String(committed.suffix(15_000))
    }
}

//
// BeBoBShellViewModel.swift — ViewModel for the BeBoB Virtual UART & Telemetry Dashboard.

import Foundation
import Combine

final class BeBoBShellViewModel: ObservableObject {
    @Published var isConnected: Bool = false
    @Published var isExecuting: Bool = false
    @Published var terminalOutput: String = ""
    /// The shell prints its own prompt and it is not the same on every BeBoB
    /// device — `/cfg>` on the TerraTec PHASE 88, `1814>` on the M-Audio. Learn
    /// it from the device's output instead of hard-coding one model's.
    @Published private(set) var devicePrompt: String = ">"
    @Published var commandInput: String = ""
    @Published var streamingStats: BeBoBStreamingStats? = nil
    @Published var avStat: BeBoBAvStat? = nil
    @Published var syncState: BeBoBSyncState? = nil
    @Published var availableDevices: [FWDeviceInfo] = []
    @Published var selectedDeviceID: DeviceInstanceID = DeviceInstanceID(1)
    @Published var errorMessage: String? = nil

    private var screen = VirtualUartScreen()
    var commandHistory: [String] = []
    var historyIndex: Int = -1
    private var isRefreshingTelemetry = false

    private let connector: ASFWDriverConnector
    private var cancellables = Set<AnyCancellable>()

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
        // Three gated conversations take well over a second, so a second press
        // while one is in flight would only queue behind the mailbox gate.
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

    func clearTerminal() {
        screen.reset()
        terminalOutput = ""
    }

    /// The trailing line of a completed response is the shell's prompt. Echo can
    /// never be mistaken for it: every echo redraw ends with a space and a
    /// backspace, not with `>`.
    static func parsePrompt(from output: String) -> String? {
        // Scan scalars, not Characters: Swift treats CRLF as a single grapheme
        // cluster, so comparing a Character against "\r" or "\n" never matches the
        // line endings this device actually emits.
        var tail: [Unicode.Scalar] = []
        for scalar in output.unicodeScalars.reversed() {
            if scalar == "\r" || scalar == "\n" { break }
            tail.append(scalar)
        }
        let candidate = String(String.UnicodeScalarView(tail.reversed()))
            .trimmingCharacters(in: .whitespaces)
        guard candidate.hasSuffix(">"), candidate.unicodeScalars.count <= 32,
              !candidate.unicodeScalars.contains(where: { $0.value < 0x20 }) else {
            return nil
        }
        return candidate
    }

    private func appendToTerminal(_ text: String) {
        // Parse the prompt from the RAW bytes: the drain stops at the prompt, so
        // the untouched tail is exactly it. The rendered text ends mid-line once
        // the next command starts echoing.
        if let prompt = Self.parsePrompt(from: text) {
            devicePrompt = prompt
        }
        screen.append(text)
        terminalOutput = screen.rendered
    }
}
