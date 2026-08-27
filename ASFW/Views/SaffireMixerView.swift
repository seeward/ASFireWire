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
                SaffireMixerBusSection(matrix: matrix, connector: connector, controls: controls,
                                       outputRole: .monitorMix)
                if matrix.outputs.contains(where: { $0.outputRole == .effectSend }) {
                    SaffireMixerBusSection(matrix: matrix, connector: connector, controls: controls,
                                           outputRole: .effectSend)
                }
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
        // The vendor calls all four analog inputs "Anlg In n"; "LINE n" would
        // both rename them and imply a mode the signal identity does not carry.
        case .analogLine: return "ANLG IN \(axis.signalIndex)"
        case .hostStream: return "DAW \(axis.signalIndex)"
        case .digitalSpdif: return "S/PDIF \(axis.signalIndex)"
        case .digitalAdat: return "ADAT \(axis.signalIndex)"
        // Several vendor categories share the auxiliary kind, which carries no
        // sub-kind of its own, so they are separated by the disjoint index
        // ranges the driver's signal table allocates. A future matrix revision
        // should publish the vendor category and retire this mapping.
        case .auxiliary:
            switch axis.signalIndex {
            case 1...2: return "FX (ANLG \(axis.signalIndex))"
            case 3...4: return "REVERB \(axis.signalIndex - 2)"
            case 5...12: return "MIX \(axis.signalIndex - 4)"
            case 13...14: return "RVB SEND \(axis.signalIndex - 12)"
            case 15...16: return "ARM \(axis.signalIndex - 14)"
            case 17: return "OFF"
            default: return "AUX \(axis.signalIndex)"
            }
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

/// Projection of driver-published, router-reachable SPro mixer destinations.
/// Raw TCAT rows that reach no active destination stay below the semantic seam.
/// Product grouping and the verified stereo write law are driver-owned.
private struct SaffireMixerBusSection: View {
    let matrix: AudioSemanticMatrixSnapshot
    let connector: ASFWDriverConnector
    let controls: SaffireControlSurface?
    let outputRole: AudioSemanticMatrixSnapshot.OutputRole
    @State private var selectedGroupID: UInt32?
    @State private var writeInFlight = false
    @State private var writeStatus: String?

    private var destinationGroups: [SaffireMatrixGroup] {
        SaffireMatrixGroup.make(from: matrix.outputs, role: outputRole)
    }

    private var selectedGroup: SaffireMatrixGroup? {
        guard !destinationGroups.isEmpty else { return nil }
        return destinationGroups.first(where: { $0.id == selectedGroupID }) ?? destinationGroups[0]
    }

    private var cardTitle: String {
        outputRole == .effectSend ? "Reverb Send" : "Hardware Monitor Mixer"
    }

    private var sectionTitle: String {
        outputRole == .effectSend ? "Reverb input sends" : "Monitor sends"
    }

    private var sectionDescription: String {
        if outputRole == .effectSend {
            return "Stereo hardware effects bus · mono level/pan and stereo level/balance"
        }
        return selectedGroup?.axes.count == 2
            ? "Stereo bus · mono level/pan and stereo level/balance"
            : "Mono bus · native coefficient readback"
    }

    var body: some View {
        AudioTopologyCard(title: cardTitle,
                          systemImage: outputRole == .effectSend ? "waveform.path.ecg" : "slider.vertical.3",
                          badge: "Hardware") {
            if let selectedGroup {
                VStack(alignment: .leading, spacing: 12) {
                    HStack(alignment: .firstTextBaseline) {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(sectionTitle)
                                .font(.headline)
                            Text(sectionDescription)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        SaffireMixerDestinationBadge(group: selectedGroup, controls: controls)
                    }

                    HStack(spacing: 7) {
                        ForEach(destinationGroups) { group in
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
                                                    },
                                                    onSuppress: { muted, soloed in
                                                        submitSuppression(source: source,
                                                                          destination: selectedGroup,
                                                                          muted: muted, soloed: soloed)
                                                    })
                            }
                        }
                        .padding(.vertical, 2)
                    }

                    Text(writeStatus ?? "A grouped gesture is exactly two native crosspoint writes followed by an exact readback. The driver decides whether a source is writable level/pan, writable level/balance, readback-only, or hidden.")
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                }
            } else {
                Text("The active router has not published this mixer destination.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .onChange(of: destinationGroups.map(\.id)) { _, ids in
            if let selectedGroupID, ids.contains(selectedGroupID) { return }
            selectedGroupID = ids.first
        }
    }

    private var sourceGroups: [SaffireMatrixGroup] {
        guard let selectedGroup else { return [] }
        return SaffireMatrixGroup.make(from: matrix.inputs, role: nil).filter {
            SaffireStripPresentation.resolve(matrix: matrix, source: $0,
                                              destination: selectedGroup) != nil
        }
    }

    private func submit(source: SaffireMatrixGroup, destination: SaffireMatrixGroup,
                        levelMilliDb: Int32, balanceMilli: Int32) {
        guard SaffireStripPresentation.resolve(matrix: matrix, source: source,
                                               destination: destination)?.isWritable == true,
              !writeInFlight else { return }
        writeInFlight = true
        writeStatus = "Writing (source.title) → (destination.title)…"
        connector.submitAudioSemanticMatrixGroupedStrip(
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

    private func submitSuppression(source: SaffireMatrixGroup, destination: SaffireMatrixGroup,
                                   muted: Bool, soloed: Bool) {
        guard SaffireStripPresentation.resolve(matrix: matrix, source: source,
                                               destination: destination)?.isWritable == true,
              !writeInFlight else { return }
        writeInFlight = true
        // Solo rewrites the whole bus, so this is not always the two writes a
        // level gesture costs.
        writeStatus = soloed || muted
            ? "Silencing (destination.title)…"
            : "Restoring (destination.title)…"
        connector.submitAudioSemanticMatrixStripSuppression(
            endpointID: matrix.endpointID,
            outputPresentationGroupID: destination.id,
            inputPresentationGroupID: source.id,
            muted: muted,
            soloed: soloed
        ) { status in
            writeInFlight = false
            writeStatus = status == KERN_SUCCESS
                ? "Confirmed by hardware readback."
                : "Write rejected: \(connector.interpretIOReturn(status))"
        }
    }
}

private enum SaffireStripPresentation {
    case monoLevelPan
    case stereoLevelBalance
    case scalarReadback

    var isWritable: Bool {
        self != .scalarReadback
    }

    static func resolve(matrix: AudioSemanticMatrixSnapshot,
                        source: SaffireMatrixGroup,
                        destination: SaffireMatrixGroup) -> SaffireStripPresentation? {
        guard let outputLeft = destination.member(role: .left),
              let outputRight = destination.member(role: .right) else { return nil }

        if source.axes.count == 1,
           let input = source.axes.first,
           input.axis.channelRole == .mono {
            let presentations = [
                matrix.crosspointPresentation(output: outputLeft.index, input: input.index),
                matrix.crosspointPresentation(output: outputRight.index, input: input.index),
            ]
            if presentations.allSatisfy({ $0 == .monoLevelPan }) { return .monoLevelPan }
            return presentations.contains(where: { $0 != nil && $0 != .hidden })
                ? .scalarReadback : nil
        }

        if let inputLeft = source.member(role: .left),
           let inputRight = source.member(role: .right) {
            let diagonal = [
                matrix.crosspointPresentation(output: outputLeft.index, input: inputLeft.index),
                matrix.crosspointPresentation(output: outputRight.index, input: inputRight.index),
            ]
            if diagonal.allSatisfy({ $0 == .stereoLevelBalance }) {
                return .stereoLevelBalance
            }
            let allCells = destination.axes.flatMap { output in
                source.axes.map { input in
                    matrix.crosspointPresentation(output: output.index, input: input.index)
                }
            }
            return allCells.contains(where: { $0 != nil && $0 != .hidden })
                ? .scalarReadback : nil
        }
        return nil
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

    func member(role: AudioSemanticMatrixSnapshot.ChannelRole) -> Member? {
        axes.first(where: { $0.axis.channelRole == role })
    }

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

private struct SaffireMixerDestinationBadge: View {
    let group: SaffireMatrixGroup
    let controls: SaffireControlSurface?

    private var routesSelectedBus: Bool {
        guard group.axes.count == 2,
              group.axes.first?.axis.signalIndex == 1 else { return false }
        return controls?.outputPairs.contains(where: { $0.routeSource == .mixer12 }) == true
    }

    var body: some View {
        let isEffectSend = group.outputRole == .effectSend
        let active = isEffectSend || routesSelectedBus
        Label(isEffectSend ? "Feeds hardware reverb"
                           : (active ? "Routed to physical output" : "Not routed to physical output"),
              systemImage: active ? "arrow.right.circle.fill" : "arrow.right.circle")
            .font(.caption.weight(.semibold))
            .foregroundStyle(active ? Color.green : Color.secondary)
    }
}

private struct SaffireStripSignature: Equatable {
    let levelMilliDb: Int32
    let balanceMilli: Int32
    let muted: Bool
    let soloed: Bool
}

private struct SaffireMonitorStrip: View {
    let matrix: AudioSemanticMatrixSnapshot
    let source: SaffireMatrixGroup
    let destination: SaffireMatrixGroup
    let writeInFlight: Bool
    let onCommit: (Int32, Int32) -> Void
    let onSuppress: (Bool, Bool) -> Void

    @State private var levelMilliDb: Int32
    @State private var balanceMilli: Int32

    private var tint: Color { SaffireSignalLabel.tint(source.axes[0].axis) }
    private var presentation: SaffireStripPresentation {
        SaffireStripPresentation.resolve(matrix: matrix, source: source,
                                         destination: destination) ?? .scalarReadback
    }

    private var stripState: AudioSemanticMatrixSnapshot.StripState? {
        matrix.stripState(output: destination.id, input: source.id)
    }
    private var isMuted: Bool { stripState?.muted ?? false }
    private var isSoloed: Bool { stripState?.soloed ?? false }
    /// Silenced by another strip's solo rather than by its own mute. Worth
    /// showing distinctly: the strip is not muted, and un-muting it will not
    /// bring it back.
    private var isDimmedBySolo: Bool {
        !isMuted && !isSoloed && matrix.busHasSolo(output: destination.id)
    }

    init(matrix: AudioSemanticMatrixSnapshot, source: SaffireMatrixGroup,
         destination: SaffireMatrixGroup, writeInFlight: Bool,
         onCommit: @escaping (Int32, Int32) -> Void,
         onSuppress: @escaping (Bool, Bool) -> Void) {
        self.matrix = matrix
        self.source = source
        self.destination = destination
        self.writeInFlight = writeInFlight
        self.onCommit = onCommit
        self.onSuppress = onSuppress
        let projected = Self.project(matrix: matrix, source: source, destination: destination)
        _levelMilliDb = State(initialValue: projected.levelMilliDb)
        _balanceMilli = State(initialValue: projected.balanceMilli)
    }

    var body: some View {
        AudioConsoleStripShell(title: source.title, tint: tint,
                               width: presentation.isWritable ? 156 : 116) {
            switch presentation {
            case .monoLevelPan:
                monoControl
            case .stereoLevelBalance:
                stereoControl
            case .scalarReadback:
                monoReadback
            }
        }
        .onChange(of: stripSignature) { _, _ in
            guard !writeInFlight else { return }
            let projected = Self.project(matrix: matrix, source: source, destination: destination)
            levelMilliDb = projected.levelMilliDb
            balanceMilli = projected.balanceMilli
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(source.title) mixer control")
    }

    /// Mute and solo are absolute, not toggles, at the wire: each tap sends the
    /// state it wants. Solo silences the rest of this bus only.
    private var suppressionButtons: some View {
        HStack(spacing: 4) {
            suppressionButton(title: "M", isOn: isMuted, tint: .orange,
                              accessibility: "\(source.title) mute") {
                onSuppress(!isMuted, isSoloed)
            }
            suppressionButton(title: "S", isOn: isSoloed, tint: .yellow,
                              accessibility: "\(source.title) solo") {
                onSuppress(isMuted, !isSoloed)
            }
        }
    }

    private func suppressionButton(title: String, isOn: Bool, tint: Color,
                                   accessibility: String,
                                   action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.caption2.monospaced().bold())
                .foregroundStyle(isOn ? Color.black : Color.secondary)
                .frame(width: 26, height: 16)
                .background(isOn ? tint : Color.white.opacity(0.08),
                            in: RoundedRectangle(cornerRadius: 3))
        }
        .buttonStyle(.plain)
        .disabled(writeInFlight)
        .accessibilityLabel(accessibility)
        .accessibilityAddTraits(isOn ? .isSelected : [])
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
        AudioConsoleStripSlots(controlHeight: 62, faderHeight: 190) {
            VStack(spacing: 3) {
                Text("STEREO SOURCE")
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(.secondary)
                Text(isDimmedBySolo ? "SOLO ELSEWHERE" : "BALANCE + LEVEL")
                    .font(.caption2.monospaced())
                    .foregroundStyle(isDimmedBySolo ? Color.secondary : tint)
                suppressionButtons
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
    }

    private var monoControl: some View {
        AudioConsoleStripSlots(controlHeight: 62, faderHeight: 190) {
            VStack(spacing: 3) {
                Text("MONO SOURCE")
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(.secondary)
                Text(isDimmedBySolo ? "SOLO ELSEWHERE" : "PAN + LEVEL")
                    .font(.caption2.monospaced())
                    .foregroundStyle(isDimmedBySolo ? Color.secondary : tint)
                suppressionButtons
            }
        } faderBlock: {
            VStack(spacing: 6) {
                AudioTopologyKnob(
                    value: (Double(balanceMilli) + 1000) / 2000,
                    tint: tint,
                    caption: balanceLabel,
                    isBipolar: true,
                    onCommitted: { _ in onCommit(levelMilliDb, balanceMilli) },
                    onChanged: { balanceMilli = Self.detented(-1000 + $0 * 2000) }
                )
                .disabled(writeInFlight)
                .accessibilityLabel("\(source.title) pan")

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
    }

    private func coefficient(output: Int, input: Int) -> UInt16 {
        matrix.coefficient(output: output, input: input) ?? 0
    }

    /// What the controls should be showing. Watching the projected value rather
    /// than the raw cells means a mute -- which zeroes the cells while the
    /// remembered level is unchanged -- correctly leaves the fader where it was.
    private var stripSignature: SaffireStripSignature {
        let projected = Self.project(matrix: matrix, source: source, destination: destination)
        return .init(levelMilliDb: projected.levelMilliDb,
                     balanceMilli: projected.balanceMilli,
                     muted: isMuted, soloed: isSoloed)
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

    /// A suppressed strip's cells read zero, so the fader has to follow the
    /// level the driver remembers instead of the silence on the wire -- if it
    /// followed the wire, muting would destroy the position it must restore.
    private static func nominal(matrix: AudioSemanticMatrixSnapshot,
                                source: SaffireMatrixGroup, destination: SaffireMatrixGroup,
                                liveLeft: UInt16, liveRight: UInt16) -> (UInt16, UInt16) {
        guard let state = matrix.stripState(output: destination.id, input: source.id) else {
            return (liveLeft, liveRight)
        }
        return (state.nominalLeft, state.nominalRight)
    }

    private static func project(matrix: AudioSemanticMatrixSnapshot, source: SaffireMatrixGroup,
                                destination: SaffireMatrixGroup) -> (levelMilliDb: Int32, balanceMilli: Int32) {
        guard let outputLeft = destination.member(role: .left),
              let outputRight = destination.member(role: .right),
              let presentation = SaffireStripPresentation.resolve(
                matrix: matrix, source: source, destination: destination) else {
            return (-85_000, 0)
        }

        if presentation == .monoLevelPan,
           let input = source.axes.first,
           let liveLeft = matrix.coefficient(output: outputLeft.index, input: input.index),
           let liveRight = matrix.coefficient(output: outputRight.index, input: input.index) {
            let (left, right) = nominal(matrix: matrix, source: source, destination: destination,
                                        liveLeft: liveLeft, liveRight: liveRight)
            let leftAmplitude = Double(left) / 16384
            let rightAmplitude = Double(right) / 16384
            let levelAmplitude = hypot(leftAmplitude, rightAmplitude)
            guard levelAmplitude > 0 else { return (-85_000, 0) }
            let levelDb = 20 * log10(levelAmplitude)
            let position = atan2(rightAmplitude, leftAmplitude) / (.pi / 2)
            return (Int32(max(-85_000, min(6_000, (levelDb * 1000).rounded()))),
                    Int32((-1000 + 2000 * position).rounded()))
        }

        guard presentation == .stereoLevelBalance,
              let inputLeft = source.member(role: .left),
              let inputRight = source.member(role: .right),
              let liveLeft = matrix.coefficient(output: outputLeft.index, input: inputLeft.index),
              let liveRight = matrix.coefficient(output: outputRight.index, input: inputRight.index) else {
            return (-85_000, 0)
        }
        let (left, right) = nominal(matrix: matrix, source: source, destination: destination,
                                    liveLeft: liveLeft, liveRight: liveRight)
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
