import Foundation
import SwiftUI

/// The Saffire page consumes driver-owned semantic snapshots. It deliberately
/// does not reconstruct MixControl buses from raw DICE coefficient rows.
struct SaffireMixerView: View {
    private static let saffirePro24Kind: UInt32 = 0x5350_3234 // "SP24"

    let connector: ASFWDriverConnector
    @State private var matrix: AudioSemanticMatrixSnapshot?
    @State private var controls: AudioControlSurfaceSnapshot?
    @State private var status = "Waiting for the Saffire Pro 24 DSP profile…"

    var body: some View {
        Group {
            if let matrix {
                SaffireMixerRack(matrix: matrix, connector: connector,
                                  controls: SaffireControlSurface(controls), status: status)
            } else {
                ContentUnavailableView(
                    "Saffire mixer is loading",
                    systemImage: "slider.vertical.3",
                    description: Text(status))
            }
        }
        .navigationTitle("Saffire Pro 24 DSP")
        .task { await pollSnapshots() }
    }

    private func pollSnapshots() async {
        while !Task.isCancelled {
            let endpointIDs = connector.getAudioSemanticMatrixEndpointIDs()
            var found: AudioSemanticMatrixSnapshot?
            for endpointID in endpointIDs {
                let candidate = await readMatrix(endpointID)
                if candidate?.deviceKind == Self.saffirePro24Kind {
                    found = candidate
                    break
                }
            }

            if let found {
                matrix = found
                controls = await readControls(found.endpointID)
                status = controls == nil
                    ? "Router/mixer revision \(found.stateRevision) · controls are loading"
                    : "Router/mixer revision \(found.stateRevision) · hardware readback current"
            } else if endpointIDs.isEmpty {
                status = "No published Saffire mixer endpoint."
            } else {
                status = "Driver is reading the active hardware router…"
            }
            try? await Task.sleep(for: .seconds(1))
        }
    }

    private func readMatrix(_ endpointID: AudioEndpointID) async -> AudioSemanticMatrixSnapshot? {
        await withCheckedContinuation { continuation in
            connector.requestAudioSemanticMatrix(endpointID: endpointID) { snapshot in
                continuation.resume(returning: snapshot)
            }
        }
    }

    private func readControls(_ endpointID: AudioEndpointID) async -> AudioControlSurfaceSnapshot? {
        await withCheckedContinuation { continuation in
            connector.requestAudioControlSurfaceSnapshotAsync(endpointID: endpointID) { snapshot in
                continuation.resume(returning: snapshot)
            }
        }
    }
}

/// SPro24 DSP state is split by hardware domain. The active router says which
/// source reaches each physical output; the base DICE mixer image only exposes
/// scalar coefficients. Those rows do not declare MixControl's bus grouping,
/// stereo-link state, pan law, or audible destination, so this view keeps them
/// out of the control surface until the driver can publish the complete model.
private struct SaffireMixerRack: View {
    let matrix: AudioSemanticMatrixSnapshot
    let connector: ASFWDriverConnector
    let controls: SaffireControlSurface?
    let status: String

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                SaffireConsoleStatusCard(status: status, controls: controls)
                SaffireInputOutputSection(endpointID: matrix.endpointID,
                                          connector: connector,
                                          controls: controls)
                SaffireMonitorMixerSection(matrix: matrix, connector: connector, controls: controls)
                SaffireDspSection(controls: controls)
                SaffirePatchbaySection(inputs: matrix.inputs)
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

}

enum SaffireSignalLabel {
    static func title(_ axis: AudioSemanticMatrixSnapshot.Axis) -> String {
        switch axis.signalKind {
        case .analogMicXlr: return "MIC \(axis.signalIndex)"
        case .analogInstrument: return "INST \(axis.signalIndex)"
        case .analogLine: return "LINE \(axis.signalIndex)"
        case .hostStream: return "DAW \(axis.signalIndex)"
        case .digitalSpdif: return "S/PDIF \(axis.signalIndex)"
        case .digitalAdat: return "ADAT \(axis.signalIndex)"
        case .auxiliary:
            return axis.signalIndex <= 2 ? "FX (ANLG \(axis.signalIndex))"
                                         : "REVERB \(axis.signalIndex - 2)"
        default: return "SOURCE \(axis.signalIndex)"
        }
    }

    static func tint(_ axis: AudioSemanticMatrixSnapshot.Axis) -> Color {
        switch axis.signalKind {
        case .hostStream: return .purple
        case .auxiliary: return .yellow
        case .digitalAdat, .digitalSpdif: return .cyan
        default: return .cyan
        }
    }
}

/// A read-only projection of the SPro24's hardware-declared monitor buses.
/// It exposes every real coefficient without inventing a MixControl pan law or
/// a writable logical fader. The driver groups its rows from the SPro busms
/// header before this view sees them.
private struct SaffireMonitorMixerSection: View {
    let matrix: AudioSemanticMatrixSnapshot
    let connector: ASFWDriverConnector
    let controls: SaffireControlSurface?
    @State private var selectedGroupID: UInt32?
    @State private var writeInFlight = false
    @State private var writeStatus: String?

    private var monitorGroups: [SaffireMatrixGroup] {
        SaffireMatrixGroup.make(from: matrix.outputs, role: .monitorMix)
    }

    private var selectedGroup: SaffireMatrixGroup? {
        guard !monitorGroups.isEmpty else { return nil }
        return monitorGroups.first(where: { $0.id == selectedGroupID }) ?? monitorGroups[0]
    }

    var body: some View {
        AudioTopologyCard(title: "Hardware Monitor Mixer",
                          systemImage: "slider.vertical.3",
                          badge: "Readback") {
            if let selectedGroup {
                VStack(alignment: .leading, spacing: 12) {
                    HStack(alignment: .firstTextBaseline) {
                        VStack(alignment: .leading, spacing: 3) {
                            Text("Monitor sends")
                                .font(.headline)
                            Text(selectedGroup.axes.count == 2
                                 ? "Stereo bus · one level and balance per verified stereo source"
                                 : "Mono bus · native coefficient readback")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        SaffireMonitorRouteBadge(group: selectedGroup, controls: controls)
                    }

                    HStack(spacing: 7) {
                        ForEach(monitorGroups) { group in
                            let isSelected = selectedGroup.id == group.id
                            Button(group.title) { selectedGroupID = group.id }
                                .buttonStyle(.plain)
                                .font(.caption.monospaced().bold())
                                .foregroundStyle(isSelected ? Color.black : Color.secondary)
                                .padding(.horizontal, 13)
                                .padding(.vertical, 6)
                                .background(isSelected ? Color.orange : Color.white.opacity(0.08),
                                            in: RoundedRectangle(cornerRadius: 5))
                                .accessibilityLabel(group.title)
                                .accessibilityValue(isSelected ? "Selected" : "Not selected")
                        }
                    }

                    Text("SOURCES")
                        .font(.caption.monospaced().bold())
                        .foregroundStyle(.cyan)

                    ScrollView(.horizontal) {
                        LazyHStack(alignment: .top, spacing: 8) {
                            ForEach(sourceGroups) { source in
                                SaffireMonitorStrip(matrix: matrix,
                                                    source: source,
                                                    destination: selectedGroup,
                                                    writeInFlight: writeInFlight,
                                                    onCommit: { levelMilliDb, balanceMilli in
                                                        submit(source: source, destination: selectedGroup,
                                                               levelMilliDb: levelMilliDb,
                                                               balanceMilli: balanceMilli)
                                                    })
                            }
                        }
                        .padding(.vertical, 2)
                    }

                    Text(writeStatus ?? "A stereo gesture is exactly two native crosspoint writes followed by an exact readback. Mono and unverified groups remain read-only.")
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                }
            } else {
                Text("The SPro mixer has not published a monitor-bus model yet…")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .onChange(of: monitorGroups.map(\.id)) { _, ids in
            if let selectedGroupID, ids.contains(selectedGroupID) { return }
            selectedGroupID = ids.first
        }
    }

    private var sourceGroups: [SaffireMatrixGroup] {
        SaffireMatrixGroup.make(from: matrix.inputs, role: nil)
    }

    private func submit(source: SaffireMatrixGroup, destination: SaffireMatrixGroup,
                        levelMilliDb: Int32, balanceMilli: Int32) {
        guard source.axes.count == 2, destination.axes.count == 2, !writeInFlight else { return }
        writeInFlight = true
        writeStatus = "Writing (source.title) → (destination.title)…"
        connector.submitAudioSemanticMatrixStereoStrip(
            endpointID: matrix.endpointID,
            outputPresentationGroupID: destination.id,
            inputPresentationGroupID: source.id,
            levelMilliDb: levelMilliDb,
            balanceMilli: balanceMilli
        ) { status in
            writeInFlight = false
            writeStatus = status == KERN_SUCCESS
                ? "Confirmed by hardware readback."
                : "Write rejected: \(connector.interpretIOReturn(status))"
        }
    }
}

private struct SaffireMatrixGroup: Identifiable {
    struct Member: Identifiable {
        let index: Int
        let axis: AudioSemanticMatrixSnapshot.Axis
        var id: UInt32 { axis.id }
    }

    let id: UInt32
    let axes: [Member]
    let outputRole: AudioSemanticMatrixSnapshot.OutputRole?

    static func make(from axes: [AudioSemanticMatrixSnapshot.Axis],
                     role: AudioSemanticMatrixSnapshot.OutputRole?) -> [SaffireMatrixGroup] {
        var ordered: [UInt32] = []
        var grouped: [UInt32: [Member]] = [:]
        for (index, axis) in axes.enumerated() {
            guard role == nil || axis.outputRole == role else { continue }
            if grouped[axis.presentationGroupID] == nil { ordered.append(axis.presentationGroupID) }
            grouped[axis.presentationGroupID, default: []].append(.init(index: index, axis: axis))
        }
        return ordered.compactMap { id in
            guard let members = grouped[id] else { return nil }
            return .init(id: id, axes: members, outputRole: role)
        }
    }

    var title: String {
        guard let first = axes.first else { return "BUS" }
        switch outputRole {
        case .monitorMix:
            if let second = axes.dropFirst().first {
                return "MIX \(first.axis.signalIndex)/\(second.axis.signalIndex)"
            }
            return "MIX \(first.axis.signalIndex)"
        case .effectSend:
            return axes.count == 2 ? "REVERB SEND" : "REVERB \(first.axis.signalIndex)"
        case nil:
            if axes.count == 2 {
                let title = SaffireSignalLabel.title(first.axis)
                return title.replacingOccurrences(of: " 1", with: " 1/2")
            }
            return SaffireSignalLabel.title(first.axis)
        }
    }
}

private struct SaffireMonitorRouteBadge: View {
    let group: SaffireMatrixGroup
    let controls: SaffireControlSurface?

    private var routesSelectedBus: Bool {
        guard group.axes.count == 2,
              group.axes.first?.axis.signalIndex == 1 else { return false }
        return controls?.outputPairs.contains(where: { $0.routeSource == .mixer12 }) == true
    }

    var body: some View {
        Label(routesSelectedBus ? "Routed to physical output" : "Not routed to physical output",
              systemImage: routesSelectedBus ? "arrow.right.circle.fill" : "arrow.right.circle")
            .font(.caption.weight(.semibold))
            .foregroundStyle(routesSelectedBus ? Color.green : Color.secondary)
    }
}

private struct SaffireMonitorStrip: View {
    let matrix: AudioSemanticMatrixSnapshot
    let source: SaffireMatrixGroup
    let destination: SaffireMatrixGroup
    let writeInFlight: Bool
    let onCommit: (Int32, Int32) -> Void

    @State private var levelMilliDb: Int32
    @State private var balanceMilli: Int32

    private var tint: Color { SaffireSignalLabel.tint(source.axes[0].axis) }

    init(matrix: AudioSemanticMatrixSnapshot, source: SaffireMatrixGroup,
         destination: SaffireMatrixGroup, writeInFlight: Bool,
         onCommit: @escaping (Int32, Int32) -> Void) {
        self.matrix = matrix
        self.source = source
        self.destination = destination
        self.writeInFlight = writeInFlight
        self.onCommit = onCommit
        let projected = Self.project(matrix: matrix, source: source, destination: destination)
        _levelMilliDb = State(initialValue: projected.levelMilliDb)
        _balanceMilli = State(initialValue: projected.balanceMilli)
    }

    var body: some View {
        AudioConsoleStripShell(title: source.title, tint: tint, width: source.axes.count == 2 ? 156 : 116) {
            if source.axes.count == 2 && destination.axes.count == 2 {
                stereoControl
            } else {
                monoReadback
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(source.title) readback")
    }

    private var monoReadback: some View {
        AudioConsoleStripSlots(controlHeight: 34, faderHeight: 190) {
            VStack(spacing: 2) {
                Text("MONO SOURCE")
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(.secondary)
                Text("READBACK")
                    .font(.caption2.monospaced())
                    .foregroundStyle(tint)
            }
        } faderBlock: {
            // A mono source holds one cell in EVERY monitor row, and on this
            // device exactly one of the pair is non-zero — the pair *is* the pan
            // position. Reading only the left cell reported every right-panned
            // source as OFF while it passed signal at unity: measured on
            // hardware, 6 of 12 mono strips (MIC 2, LINE 2, ADAT 2/4/6/8). Show
            // both buses so the hard-panned structure is visible rather than
            // half-hidden. 126pt matches the stereo strip's fader so the two
            // kinds of strip line up across the rack.
            VStack(spacing: 5) {
                HStack(alignment: .bottom, spacing: 6) {
                    SaffireCoefficientReadback(
                        value: coefficient(output: destination.axes[0].index,
                                           input: source.axes[0].index),
                        title: "L", tint: tint)
                    if destination.axes.count > 1 {
                        SaffireCoefficientReadback(
                            value: coefficient(output: destination.axes[1].index,
                                               input: source.axes[0].index),
                            title: "R", tint: tint)
                    }
                }
                .frame(height: 126)
                Text("No verified grouped write")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var stereoControl: some View {
        AudioConsoleStripSlots(controlHeight: 45, faderHeight: 190) {
            VStack(spacing: 3) {
                Text("STEREO SOURCE")
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(.secondary)
                Text("BALANCE + LEVEL")
                    .font(.caption2.monospaced())
                    .foregroundStyle(tint)
            }
        } faderBlock: {
            // Sized to the 190pt slot: 37 (rotary + caption) + 126 (fader) + 13
            // (readout) + spacing. The block is bottom-aligned, so content that
            // overflows rides up over the strip header instead of clipping.
            VStack(spacing: 6) {
                // Balance is bipolar about centre, which a linear slider renders
                // as an arbitrary mid-track position. The rotary fills outward
                // from top centre, so "centred" is legible at a glance. It sits
                // above the fader because the two are both vertical drags, and
                // stacking the smaller one on top keeps their targets distinct.
                // Commits on release: one gesture is two crosspoint writes plus
                // a readback, not a continuous stream.
                AudioTopologyKnob(
                    value: (Double(balanceMilli) + 1000) / 2000,
                    tint: tint,
                    caption: balanceLabel,
                    isBipolar: true,
                    onCommitted: { _ in onCommit(levelMilliDb, balanceMilli) },
                    onChanged: { balanceMilli = Self.detented(-1000 + $0 * 2000) }
                )
                .disabled(writeInFlight)
                .accessibilityLabel("\(source.title) balance")

                AudioConsoleVerticalFader(
                    title: "\(source.title) level",
                    value: Double(levelMilliDb), range: -85_000...6_000, step: 100,
                    tint: tint, trackWidth: 6, thumbSize: CGSize(width: 28, height: 14),
                    fillsTrack: true, isEnabled: !writeInFlight,
                    valueDescription: { String(format: "%+.1f dB", $0 / 1000) },
                    onValueChanged: { levelMilliDb = Int32($0.rounded()) },
                    onValueCommitted: { onCommit(Int32($0.rounded()), balanceMilli) })
                    .frame(height: 126)
                Text(String(format: "%+.1f dB", Double(levelMilliDb) / 1000))
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(tint)
            }
        }
        .onChange(of: sourceCoefficientSignature) { _, _ in
            guard !writeInFlight else { return }
            let projected = Self.project(matrix: matrix, source: source, destination: destination)
            levelMilliDb = projected.levelMilliDb
            balanceMilli = projected.balanceMilli
        }
    }

    private func coefficient(output: Int, input: Int) -> UInt16 {
        matrix.coefficient(output: output, input: input) ?? 0
    }

    private var sourceCoefficientSignature: UInt32 {
        guard source.axes.count == 2, destination.axes.count == 2 else { return 0 }
        return UInt32(coefficient(output: destination.axes[0].index, input: source.axes[0].index)) << 16 |
               UInt32(coefficient(output: destination.axes[1].index, input: source.axes[1].index))
    }

    /// "R 0%" is a contradiction: it reads as off-centre while stating it is
    /// not. Anything that rounds to zero percent is centre, and says so.
    private var balanceLabel: String {
        let percent = Int((Double(balanceMilli) / 10).rounded())
        if percent == 0 { return "C" }
        return percent < 0 ? "L \(-percent)%" : "R \(percent)%"
    }

    /// The hardware has no balance detent, so a hand-dragged rotary settles a
    /// percent or two off centre — the capture shows a deliberate centring
    /// gesture landing at -19 milli, roughly ten Q2.14 steps, far above the
    /// quantisation floor. Snap a narrow window so exact centre is reachable
    /// by hand; it is the only balance position with a defined meaning.
    private static func detented(_ balance: Double) -> Int32 {
        let value = Int32(balance.rounded())
        return abs(value) <= centreDetentMilli ? 0 : value
    }

    private static let centreDetentMilli: Int32 = 40

    private static func project(matrix: AudioSemanticMatrixSnapshot, source: SaffireMatrixGroup,
                                destination: SaffireMatrixGroup) -> (levelMilliDb: Int32, balanceMilli: Int32) {
        guard source.axes.count == 2, destination.axes.count == 2,
              let left = matrix.coefficient(output: destination.axes[0].index, input: source.axes[0].index),
              let right = matrix.coefficient(output: destination.axes[1].index, input: source.axes[1].index) else {
            return (-85_000, 0)
        }
        let leftDb = coefficientDb(left)
        let rightDb = coefficientDb(right)
        // Both channels carry the balance law, so the position has to be
        // recovered from their *difference*, not from the quieter channel's
        // offset below the louder one. At centre the law attenuates BOTH sides
        // by -0.01 dB, so the difference is 0 while each channel sits below
        // unity; reading one channel alone maps an equal pair onto a hard pan.
        let position: Double
        let level: Double
        if left == 0 {
            position = 1
            level = rightDb
        } else if right == 0 {
            position = 0
            level = leftDb
        } else {
            position = Self.balancePosition(forDifferenceDb: leftDb - rightDb)
            level = leftDb - Self.balanceAttenuationDb(1 - position)
        }
        return (Int32(max(-85_000, min(6_000, (level * 1000).rounded()))),
                Int32((-1000 + 2000 * position).rounded()))
    }

    private static func coefficientDb(_ value: UInt16) -> Double {
        guard value != 0 else { return -85 }
        return 20 * log10(Double(value) / 16384)
    }

    /// Mirror of the driver's recovered BalanceLaw (BalanceAttenuationDb in
    /// SPro24DspSemanticMatrix.cpp). Kept here only to invert a hardware
    /// readback for display; the driver remains the authority for writes, so
    /// this must track that function rather than approximate it.
    private static func balanceAttenuationDb(_ position: Double) -> Double {
        if position > 0.5 {
            return -0.01 + ((position - 0.5) * 2) * 0.01
        }
        let fraction = (pow(Self.balanceLawBase, max(0, min(1, position * 2))) - 1)
            / (Self.balanceLawBase - 1)
        return -80 + fraction * 79.99
    }

    private static let balanceLawBase = 0.002770087

    /// Inverse of the balance law. `balanceAttenuationDb(1 - p) - balanceAttenuationDb(p)`
    /// falls monotonically from +79.99 dB at hard left to -79.99 dB at hard
    /// right, so bisection recovers the position without inverting the curved
    /// half in closed form — and maps an equal L/R pair onto centre.
    private static func balancePosition(forDifferenceDb differenceDb: Double) -> Double {
        var low = 0.0
        var high = 1.0
        for _ in 0..<48 {
            let mid = (low + high) / 2
            if balanceAttenuationDb(1 - mid) - balanceAttenuationDb(mid) > differenceDb {
                low = mid
            } else {
                high = mid
            }
        }
        return (low + high) / 2
    }
}

private struct SaffireCoefficientReadback: View {
    let value: UInt16
    let title: String
    let tint: Color

    var body: some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.caption2.monospaced().bold())
                .foregroundStyle(.secondary)
            AudioConsoleVerticalFader(
                title: title,
                value: position,
                range: 0...1,
                step: 0.01,
                tint: tint,
                trackWidth: 5,
                thumbSize: CGSize(width: 25, height: 14),
                fillsTrack: true,
                valueDescription: { _ in Self.dbString(value) }
            )
            .allowsHitTesting(false)
            .accessibilityHidden(true)
            Text(Self.dbString(value))
                .font(.caption2.monospaced().bold())
                .foregroundStyle(tint)
        }
    }

    private var position: Double {
        guard value != 0 else { return 0 }
        let amplitude = Double(value) / 16384.0
        let db = 20.0 * log10(amplitude)
        return min(1, max(0, (db + 60.0) / 72.0))
    }

    static func dbString(_ value: UInt16) -> String {
        guard value != 0 else { return "OFF" }
        let db = 20.0 * log10(Double(value) / 16384.0)
        return String(format: "%+.1f dB", db)
    }
}
