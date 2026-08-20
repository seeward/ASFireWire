import SwiftUI

private let adkConfigDeviceNames = [
    "ADK Config Lab — Duet",
    "ADK Config Lab — PHASE 88",
    "ADK Config Lab — FireWire 1814",
    "ADK Config Lab — Saffire Pro 24 DSP"
]

private struct ADKConfigRefreshResult: Sendable {
    let rows: [ADKConfigDeviceRow]
    let halDevices: [CoreAudioLabDevice]
    let failures: Int
}

private actor ADKConfigLabWorker {
    private let client = ADKConfigClient()

    func capture(names: [String]) -> ADKConfigRefreshResult {
        let halDevices = CoreAudioLabSnapshot.capture()
        var rows: [ADKConfigDeviceRow] = []
        var failures = 0

        for (slot, name) in names.enumerated() {
            do {
                let state = try client.state(slot: slot)
                let events = try client.events(slot: slot, maxEvents: 20)
                rows.append(ADKConfigDeviceRow(
                    slot: slot, name: name, state: state, events: events))
            } catch {
                failures += 1
                rows.append(ADKConfigDeviceRow(
                    slot: slot, name: name, state: nil, events: []))
            }
        }

        return ADKConfigRefreshResult(
            rows: rows, halDevices: halDevices, failures: failures)
    }

    func requestRate(slot: Int, rate: UInt32) throws {
        try client.requestSampleRate(slot: slot, rate: rate)
    }

    func requestConfiguration(slot: Int, rate: UInt32,
                              opticalInput: UInt32,
                              opticalOutput: UInt32) throws {
        try client.requestConfiguration(
            slot: slot, rate: rate, opticalInput: opticalInput,
            opticalOutput: opticalOutput)
    }
}

@MainActor
final class ADKConfigLabModel: ObservableObject {
    @Published private(set) var rows: [ADKConfigDeviceRow] =
        adkConfigDeviceNames.enumerated().map {
            ADKConfigDeviceRow(slot: $0.offset, name: $0.element,
                               state: nil, events: [])
        }
    @Published private(set) var halDevices: [CoreAudioLabDevice] = []
    @Published private(set) var status = "Not queried"
    @Published private(set) var isRequesting = false

    private var lastLoggedSequence: [Int: UInt64] = [:]
    private var lastLoggedObjectID: [Int: UInt32] = [:]
    private let worker = ADKConfigLabWorker()
    private let names = adkConfigDeviceNames
    private var coreAudioObserver: CoreAudioLabObserver?
    private var externalRefreshTask: Task<Void, Never>?

    deinit {
        externalRefreshTask?.cancel()
        coreAudioObserver?.stop()
    }

    func startObservingCoreAudio() {
        guard coreAudioObserver == nil else { return }
        coreAudioObserver = CoreAudioLabObserver { [weak self] in
            Task { @MainActor in
                self?.coreAudioStateChanged()
            }
        }
        coreAudioObserver?.start()
    }

    func stopObservingCoreAudio() {
        externalRefreshTask?.cancel()
        externalRefreshTask = nil
        coreAudioObserver?.stop()
        coreAudioObserver = nil
    }

    func refresh() async {
        let result = await worker.capture(names: names)
        guard !Task.isCancelled else { return }

        for row in result.rows {
            let newest = row.events.last?.sequence ?? 0
            let objectID = row.state?.deviceObjectID
                ?? row.events.last?.deviceObjectID
                ?? 0
            let recorded = lastLoggedSequence[row.slot] ?? 0
            let objectChanged = lastLoggedObjectID[row.slot].map {
                $0 != objectID
            } ?? false
            let previous = objectChanged || newest < recorded ? 0 : recorded
            for event in row.events where event.sequence > previous {
                ADKConfigTrace.emit(
                    "slot=\(row.slot) seq=\(event.sequence) " +
                    "phase=\(event.phaseName) \(event.mutationSummary) " +
                    "result=\(event.resultName)")
            }
            lastLoggedSequence[row.slot] = max(previous, newest)
            lastLoggedObjectID[row.slot] = objectID
        }

        rows = result.rows
        halDevices = result.halDevices
        status = result.failures == 0
            ? "Dext state refreshed"
            : "Dext unavailable for \(result.failures) device slot(s)"
    }

    func requestRate(slot: Int, rate: UInt32) async {
        guard !isRequesting else { return }
        isRequesting = true
        defer { isRequesting = false }

        status = "Requesting \(rate) Hz for slot \(slot)…"
        do {
            try await worker.requestRate(slot: slot, rate: rate)
            status = "Request accepted for slot \(slot); waiting for HAL transaction"
        } catch {
            status = error.localizedDescription
            return
        }

        // Perform/Abort is host-driven and may be asynchronous. Take two
        // snapshots so the UI shows both the immediate request and the
        // settled callback state without requiring a manual race.
        try? await Task.sleep(for: .milliseconds(150))
        guard !Task.isCancelled else { return }
        await refresh()
        try? await Task.sleep(for: .milliseconds(450))
        guard !Task.isCancelled else { return }
        await refresh()
    }

    func requestConfiguration(slot: Int, opticalInput: UInt32,
                              opticalOutput: UInt32) async {
        guard !isRequesting,
              let state = rows.first(where: { $0.slot == slot })?.state else {
            return
        }
        isRequesting = true
        defer { isRequesting = false }

        status = "Requesting optical \(opticalInput)/\(opticalOutput) for slot \(slot)…"
        do {
            try await worker.requestConfiguration(
                slot: slot, rate: state.currentSampleRate,
                opticalInput: opticalInput, opticalOutput: opticalOutput)
        } catch {
            status = error.localizedDescription
            return
        }

        try? await Task.sleep(for: .milliseconds(150))
        guard !Task.isCancelled else { return }
        await refresh()
        try? await Task.sleep(for: .milliseconds(450))
        guard !Task.isCancelled else { return }
        await refresh()
    }

    private func coreAudioStateChanged() {
        // The CoreAudio listener may receive one rate notification plus a
        // virtual-format notification per stream. Coalesce those into one
        // diagnostic-client snapshot after HAL has committed its state.
        externalRefreshTask?.cancel()
        status = "CoreAudio changed outside this UI; synchronizing…"
        externalRefreshTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(75))
            guard !Task.isCancelled else { return }
            await self?.refresh()
        }
    }
}

struct ADKConfigLabView: View {
    @StateObject private var model = ADKConfigLabModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("AudioDriverKit Configuration Transactions")
                    .font(.headline)
                Spacer()
                Button("Refresh") {
                    Task { await model.refresh() }
                }
            }

            Text("These controls request nominal sample-rate changes through the diagnostic user client. The driver mutates ADK state only in PerformDeviceConfigurationChange; packet playback is intentionally out of scope.")
                .font(.caption)
                .foregroundStyle(.secondary)

            Text(model.status)
                .font(.caption)
                .foregroundStyle(.tertiary)

            VStack(alignment: .leading, spacing: 4) {
                Text("CoreAudio HAL snapshot")
                    .font(.subheadline.weight(.semibold))
                if model.halDevices.isEmpty {
                    Text("No ADK Config Lab devices visible to CoreAudio yet")
                        .font(.caption2)
                        .foregroundStyle(.tertiary)
                } else {
                    Text(verbatim: model.halDevices.map(\.displayText).joined(separator: "\n"))
                        .font(.system(.caption2, design: .monospaced))
                        .foregroundStyle(.secondary)
                }
            }

            ForEach(model.rows) { row in
                deviceRow(row)
            }
        }
        .padding(12)
        .background(.quaternary.opacity(0.25), in: RoundedRectangle(cornerRadius: 10))
        .task {
            model.startObservingCoreAudio()
            await model.refresh()
        }
        .onDisappear {
            model.stopObservingCoreAudio()
        }
    }

    @ViewBuilder
    private func deviceRow(_ row: ADKConfigDeviceRow) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Slot \(row.slot): \(row.name)")
                        .font(.subheadline.weight(.semibold))
                    Text("current \(row.currentRateText) · \(row.pendingText)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if let state = row.state {
                        Text("optical \(opticalName(state.currentOpticalInput))/\(opticalName(state.currentOpticalOutput)) · \(state.currentInputChannels) in / \(state.currentOutputChannels) out")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                }
                Spacer()
                Button("44.1 kHz") {
                    Task {
                        await model.requestRate(slot: row.slot, rate: 44_100)
                    }
                }
                .disabled(model.isRequesting)
                Button("48 kHz") {
                    Task {
                        await model.requestRate(slot: row.slot, rate: 48_000)
                    }
                }
                .disabled(model.isRequesting)
            }

            if row.slot == 2 || row.slot == 3, let state = row.state {
                HStack(spacing: 6) {
                    Text("Optical")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Button("In ADAT") {
                        Task { await model.requestConfiguration(
                            slot: row.slot, opticalInput: 1,
                            opticalOutput: state.currentOpticalOutput) }
                    }
                    Button("In S/PDIF") {
                        Task { await model.requestConfiguration(
                            slot: row.slot, opticalInput: 2,
                            opticalOutput: state.currentOpticalOutput) }
                    }
                    Button("Out ADAT") {
                        Task { await model.requestConfiguration(
                            slot: row.slot, opticalInput: state.currentOpticalInput,
                            opticalOutput: 1) }
                    }
                    Button("Out S/PDIF") {
                        Task { await model.requestConfiguration(
                            slot: row.slot, opticalInput: state.currentOpticalInput,
                            opticalOutput: 2) }
                    }
                }
                .controlSize(.small)
                .disabled(model.isRequesting)
            }

            if row.events.isEmpty {
                Text("No dext configuration events yet")
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            } else {
                Text(verbatim: row.eventSummary)
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundStyle(.secondary)
            }
        }
        .padding(8)
        .background(.background.opacity(0.7), in: RoundedRectangle(cornerRadius: 7))
    }

    private func opticalName(_ value: UInt32) -> String {
        switch value {
        case 1: return "ADAT"
        case 2: return "S/PDIF"
        default: return "—"
        }
    }
}
