// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBShellView.swift — Interactive Virtual UART Terminal & Telemetry Dashboard for BeBoB.

import SwiftUI

struct BeBoBShellView: View {
    @ObservedObject var viewModel: BeBoBShellViewModel
    @State private var selectedTab: Int = 0

    init(viewModel: BeBoBShellViewModel) {
        self.viewModel = viewModel
    }

    var body: some View {
        VStack(spacing: 0) {
            // Header: Device & Connection Status Bar
            headerBar

            Divider()

            // Main Content: Tabs for Dashboard vs Raw Terminal
            TabView(selection: $selectedTab) {
                telemetryDashboard
                    .tabItem {
                        Label("Telemetry Dashboard", systemImage: "gauge.with.needle")
                    }
                    .tag(0)

                terminalView
                    .tabItem {
                        Label("Virtual UART Terminal", systemImage: "terminal")
                    }
                    .tag(1)
            }
            .padding(12)
        }
        .navigationTitle("BeBoB Diagnostics & Shell")
        .onAppear {
            viewModel.refreshTelemetry()
            viewModel.drainFIFO()
        }
    }

    // MARK: - Header Bar

    private var headerBar: some View {
        HStack(spacing: 16) {
            Image(systemName: "cpu")
                .font(.title2)
                .foregroundStyle(.blue)

            VStack(alignment: .leading, spacing: 2) {
                Text("BridgeCo DM1000 / BeBoB Virtual UART")
                    .font(.headline)
                if !viewModel.availableDevices.isEmpty {
                    HStack(spacing: 8) {
                        Picker("Target Device:", selection: $viewModel.selectedDeviceID) {
                            ForEach(viewModel.availableDevices) { device in
                                Text("\(device.vendorName) \(device.modelName) [ID: \(device.id.rawValue), Node: \(device.nodeId), Gen: \(device.generation)]")
                                    .tag(device.id)
                            }
                        }
                        .pickerStyle(.menu)
                        .font(.caption)
                    }
                } else {
                    Text("Device Instance ID: \(viewModel.selectedDeviceID.rawValue) (0xFFFF_C802_1000/9000)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Spacer()

            // Auto-polling Toggle
            Button {
                viewModel.toggleAutoPolling()
            } label: {
                Label(
                    viewModel.isAutoPolling ? "Live Polling (1s)" : "Auto-Poll Paused",
                    systemImage: viewModel.isAutoPolling ? "antenna.radiowaves.left.and.right" : "play.circle"
                )
            }
            .buttonStyle(.bordered)
            .tint(viewModel.isAutoPolling ? .green : .secondary)

            // Manual Refresh
            Button {
                viewModel.refreshTelemetry()
            } label: {
                Label("Refresh", systemImage: "arrow.clockwise")
            }
            .buttonStyle(.borderedProminent)
            .disabled(!viewModel.isConnected || viewModel.isExecuting)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    // MARK: - Telemetry Dashboard Tab

    private var telemetryDashboard: some View {
        ScrollView {
            VStack(spacing: 16) {
                // Top Status Cards: Audio State & PLL Sync
                HStack(spacing: 16) {
                    syncStateCard
                    siliconLockCard
                }

                // Streaming Error Flags & Counters
                streamingMetricsSection

                // Quick Action Bar
                quickActionsSection
            }
            .padding(.vertical, 8)
        }
    }

    private var syncStateCard: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Label("Audio State", systemImage: "waveform.circle")
                        .font(.headline)
                    Spacer()
                    let stateStr = viewModel.syncState?.audioState ?? "Unknown"
                    Text(stateStr)
                        .font(.subheadline.bold())
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(stateBadgeColor(stateStr).opacity(0.2))
                        .foregroundStyle(stateBadgeColor(stateStr))
                        .clipShape(Capsule())
                }

                Divider()

                HStack {
                    Text("Sync Source:")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Text(viewModel.syncState?.syncSource ?? "—")
                        .font(.subheadline.monospaced())
                }

                HStack {
                    Text("Sample Rate:")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Spacer()
                    let rate = viewModel.syncState?.sampleRateHz ?? 0
                    Text(rate > 0 ? "\(rate) Hz" : "—")
                        .font(.subheadline.monospaced())
                }
            }
            .padding(6)
        } label: {
            Text("Clock & Engine Synchronization")
        }
    }

    private var siliconLockCard: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Label("Silicon Hardware Latches", systemImage: "lock.shield")
                        .font(.headline)
                    Spacer()
                    if let av = viewModel.avStat {
                        Text(av.setTgInLock ? "LOCKED" : "UNLOCKED")
                            .font(.caption.bold())
                            .padding(.horizontal, 6)
                            .padding(.vertical, 3)
                            .background(av.setTgInLock ? Color.green.opacity(0.2) : Color.red.opacity(0.2))
                            .foregroundStyle(av.setTgInLock ? .green : .red)
                            .clipShape(Capsule())
                    }
                }

                Divider()

                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 8) {
                    flagBadge(name: "SetTgInLock", isGood: viewModel.avStat?.setTgInLock ?? false)
                    flagBadge(name: "SYT In Window", isGood: !(viewModel.avStat?.setTgSytMiss ?? false))
                    flagBadge(name: "DBC In-Phase", isGood: !(viewModel.avStat?.dbcMismatch ?? false))
                    flagBadge(name: "FMT Valid", isGood: !(viewModel.avStat?.fmtMismatch ?? false))
                    flagBadge(name: "CIP Valid", isGood: !(viewModel.avStat?.cipMismatch ?? false))
                    flagBadge(name: "Header Valid", isGood: !(viewModel.avStat?.headerMismatch ?? false))
                }
            }
            .padding(6)
        } label: {
            Text("DM1000 Framer / TGEN Latches — sticky; clear with `sys avstat clr all`, soak, re-read")
        }
    }

    private var streamingMetricsSection: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 12) {
                Label("Streaming Cadence & Presentation Telemetry (sys stat)", systemImage: "chart.bar.xaxis")
                    .font(.headline)

                Divider()

                // `sys stat` prints one column per isochronous stream, and the
                // FireWire-facing one is not the first: on the 1814 the device's
                // internal S/PDIF-ADAT path is printed ahead of ours. Each card
                // names the iso channel it came from so the two can't be mixed.
                if let ours = viewModel.streamingStats?.fireWireInput {
                    streamMetricGrid(
                        title: "Host → device (our transmit), iso ch \(ours.isoChannel)",
                        labels: ["rxPackets", "onlyHeaders", "rxEmptyPkt", "rxNoMem",
                                 "BCOHdrErr", "CtrDiffErr", "SytDiffErr", "rxQFillLevel"],
                        column: ours
                    )
                }

                if let ours = viewModel.streamingStats?.fireWireOutput {
                    streamMetricGrid(
                        title: "Device → host (our capture), iso ch \(ours.isoChannel)",
                        labels: ["txWrite", "txDelayed", "txQFillLevel", "txQEmpty",
                                 "pkt Future", "pkt Past", "SytOffset", "SytCorr"],
                        column: ours
                    )
                }

                if viewModel.streamingStats?.noActiveOutputStreams == true {
                    Text("No active output streams.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(6)
        }
    }

    private func streamMetricGrid(title: String, labels: [String],
                                  column: BeBoBStreamColumn) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.caption.bold())
                .foregroundStyle(.secondary)

            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()),
                                GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                ForEach(labels, id: \.self) { label in
                    if let value = column.counters[label] {
                        metricCard(
                            title: BeBoBStreamColumn.percentLabels.contains(label) ? "\(label) %" : label,
                            value: "\(value)",
                            color: metricColor(label: label, value: value, column: column)
                        )
                    }
                }
            }
        }
    }

    /// Colour follows the severity character the firmware itself printed for the
    /// row ("W", "E", "S"), rather than a hardcoded opinion per counter.
    private func metricColor(label: String, value: Int64,
                             column: BeBoBStreamColumn) -> Color {
        guard value > 0 else { return .green }
        switch column.severity[label] {
        case "E": return .red
        case "W": return .orange
        case "S": return .teal
        default: return .blue
        }
    }

    private var quickActionsSection: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 10) {
                Label("Quick Diagnostic Shell Actions", systemImage: "bolt.horizontal.circle")
                    .font(.headline)

                Divider()

                HStack(spacing: 12) {
                    quickButton(title: "📊 Stream Stats", cmd: "sys stat")
                    quickButton(title: "🔒 Silicon Status", cmd: "sys avstat all")
                    quickButton(title: "⏱ Clock & Sync", cmd: "fw sync show")
                    quickButton(title: "📝 Toggle SytLog", cmd: "sys sytlog")
                    quickButton(title: "🧵 RTOS Tasks", cmd: "os th")
                    quickButton(title: "🧹 Reset Stats", cmd: "sys stat reset")
                }
            }
            .padding(6)
        }
    }

    // MARK: - Virtual UART Monospace Terminal Tab

    private var terminalView: some View {
        VStack(spacing: 8) {
            // Monospace Output Text Area
            ScrollViewReader { proxy in
                ScrollView {
                    Text(viewModel.terminalOutput)
                        .font(.system(.body, design: .monospaced))
                        .foregroundStyle(Color.green)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                        .id("bottomID")
                }
                .background(Color.black)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .onChange(of: viewModel.terminalOutput) {
                    withAnimation {
                        proxy.scrollTo("bottomID", anchor: .bottom)
                    }
                }
            }

            // Command Input Bar
            HStack(spacing: 8) {
                Text("1814>")
                    .font(.system(.body, design: .monospaced).bold())
                    .foregroundStyle(.secondary)

                TextField("Enter shell command (e.g. sys stat, fw sync show, os th)...", text: $viewModel.commandInput)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.body, design: .monospaced))
                    .onSubmit {
                        viewModel.sendCommand()
                    }

                Button {
                    viewModel.sendCommand()
                } label: {
                    Label("Send", systemImage: "paperplane.fill")
                }
                .buttonStyle(.borderedProminent)
                .disabled(viewModel.commandInput.isEmpty || viewModel.isExecuting)

                Button {
                    viewModel.drainFIFO()
                } label: {
                    Label("Drain Logs", systemImage: "arrow.down.doc")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.isExecuting)

                Button {
                    viewModel.clearTerminal()
                } label: {
                    Label("Clear", systemImage: "trash")
                }
                .buttonStyle(.bordered)
            }
        }
    }

    // MARK: - Helpers & UI Elements

    private func metricCard(title: String, value: String, color: Color) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title3.monospaced().bold())
                .foregroundStyle(color)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(8)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }

    private func flagBadge(name: String, isGood: Bool) -> some View {
        HStack {
            Image(systemName: isGood ? "checkmark.circle.fill" : "xmark.circle.fill")
                .foregroundStyle(isGood ? .green : .red)
            Text(name)
                .font(.caption.monospaced())
            Spacer()
        }
        .padding(6)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 4))
    }

    private func quickButton(title: String, cmd: String) -> some View {
        Button {
            viewModel.sendCommand(cmd)
            selectedTab = 1 // Switch to terminal to see result
        } label: {
            Text(title)
                .font(.caption)
        }
        .buttonStyle(.bordered)
    }

    private func stateBadgeColor(_ state: String) -> Color {
        switch state {
        case "Running": return .green
        case "Waiting for sync": return .orange
        case "Idle": return .blue
        case "Stop": return .red
        default: return .secondary
        }
    }
}
