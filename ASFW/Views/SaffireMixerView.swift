import Foundation
import SwiftUI

/// The Saffire page consumes the driver-owned active router projection. It
/// deliberately has no DICE register knowledge and does not reconstruct a
/// graph from raw coefficients in Swift.
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
        .task { await pollMatrix() }
    }

    private func pollMatrix() async {
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
                    ? "Mixer revision \(found.stateRevision) · control readback is still loading"
                    : "Mixer revision \(found.stateRevision) · hardware controls are current"
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

/// One selected hardware stereo mix bus at a time. The Saffire has a dense
/// 18 × 16 matrix, but presenting all sixteen destinations at once turns a
/// console into a spreadsheet. Pair selection retains every real crosspoint
/// while keeping each source strip readable.
private struct SaffireMixerRack: View {
    let matrix: AudioSemanticMatrixSnapshot
    let connector: ASFWDriverConnector
    let controls: SaffireControlSurface?
    let status: String
    @State private var selectedPair = 0

    private var pairCount: Int { matrix.outputs.count / 2 }
    private var safePair: Int { min(max(0, selectedPair), max(0, pairCount - 1)) }
    private var leftOutput: Int { safePair * 2 }
    private var rightOutput: Int { leftOutput + 1 }
    private var mixName: String { "MIX \(leftOutput + 1)/\(rightOutput + 1)" }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                SaffireConsoleStatusCard(status: status, controls: controls)
                SaffireInputOutputSection(endpointID: matrix.endpointID,
                                          connector: connector,
                                          controls: controls)
                mixerSection
                SaffireDspSection(controls: controls)
                SaffirePatchbaySection(inputs: matrix.inputs)
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onChange(of: pairCount) { _, count in
            selectedPair = min(selectedPair, max(0, count - 1))
        }
    }

    private var mixerSection: some View {
        AudioTopologyCard(title: "Hardware Monitor Mixer",
                          systemImage: "slider.vertical.3",
                          badge: mixName) {
            VStack(alignment: .leading, spacing: 14) {
                HStack(alignment: .firstTextBaseline) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Stereo monitor sends")
                            .font(.headline)
                        Text("Choose a hardware Mix pair; each source strip has one independent send to L and R.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text("Q2.14 · 0 dB = 0x4000")
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                }

                mixSelector

                ScrollView(.horizontal) {
                    AudioConsoleRackBank(title: "SOURCES", tint: .cyan) {
                        ForEach(Array(matrix.inputs.enumerated()), id: \.element.id) { index, axis in
                            SaffireSourceStrip(
                                axis: axis,
                                mixName: mixName,
                                left: matrix.coefficient(output: leftOutput, input: index) ?? 0,
                                right: matrix.coefficient(output: rightOutput, input: index) ?? 0,
                                gainLaw: matrix.gainLaw
                            )
                        }
                    }
                    .padding(.vertical, 2)
                }
                .scrollIndicators(.visible)

                Text("Mixer write is intentionally disabled until its bounded transaction, commit edge, and readback have been verified on this hardware.")
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var mixSelector: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(0..<pairCount, id: \.self) { pair in
                    let first = pair * 2 + 1
                    AudioConsoleStripToggle(
                        title: "MIX \(first)/\(first + 1)",
                        accessibilityLabel: "Select Mix \(first) through \(first + 1)",
                        isOn: safePair == pair,
                        isEnabled: true,
                        tint: .orange
                    ) {
                        selectedPair = pair
                    }
                    .frame(width: 86)
                }
            }
        }
    }
}

private struct SaffireSourceStrip: View {
    let axis: AudioSemanticMatrixSnapshot.Axis
    let mixName: String
    let left: UInt16
    let right: UInt16
    let gainLaw: AudioSemanticMatrixSnapshot.GainLaw

    private enum Metrics {
        static let width: CGFloat = 142
        static let faderHeight: CGFloat = 194
    }

    private var title: String { SaffireSignalLabel.title(axis) }
    private var tint: Color { SaffireSignalLabel.tint(axis) }

    var body: some View {
        AudioConsoleStripShell(title: title, tint: tint, width: Metrics.width) {
            AudioConsoleStripSlots(controlHeight: 34, faderHeight: Metrics.faderHeight) {
                VStack(spacing: 2) {
                    Text(mixName)
                        .font(.system(size: 9, weight: .bold).monospaced())
                        .foregroundStyle(.secondary)
                    Text(SaffireSignalLabel.detail(axis))
                        .font(.system(size: 8).monospaced())
                        .foregroundStyle(.secondary)
                }
            } faderBlock: {
                HStack(alignment: .center, spacing: 5) {
                    sendFader("→ L", coefficient: left)
                    SaffireQ214FaderScale()
                    sendFader("→ R", coefficient: right)
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(title), sends to \(mixName)")
    }

    private func sendFader(_ side: String, coefficient: UInt16) -> some View {
        VStack(spacing: 3) {
            Text(side)
                .font(.system(size: 9, weight: .bold).monospaced())
                .foregroundStyle(.secondary)
            AudioConsoleVerticalFader(
                title: "\(title) \(side) send",
                value: SaffireQ214.decibels(coefficient, gainLaw: gainLaw) ?? -60,
                range: -60...12,
                step: 0.1,
                tint: tint,
                trackWidth: 5,
                thumbSize: CGSize(width: 28, height: 13),
                fillsTrack: true,
                isEnabled: false,
                valueDescription: { _ in SaffireQ214.format(coefficient, gainLaw: gainLaw) }
            )
            Text(SaffireQ214.format(coefficient, gainLaw: gainLaw))
                .font(.system(size: 9, weight: .semibold).monospaced())
                .foregroundStyle(tint)
                .lineLimit(1)
        }
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
            return axis.signalIndex <= 2 ? "CH STRIP \(axis.signalIndex)"
                                         : "REVERB \(axis.signalIndex - 2)"
        default: return "SOURCE \(axis.signalIndex)"
        }
    }

    static func detail(_ axis: AudioSemanticMatrixSnapshot.Axis) -> String {
        switch axis.signalKind {
        case .auxiliary: return axis.signalIndex <= 2 ? "DSP RETURN" : "FX RETURN"
        case .hostStream: return "HOST PLAYBACK"
        case .digitalAdat, .digitalSpdif: return "DIGITAL INPUT"
        default: return "ANALOG INPUT"
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

private enum SaffireQ214 {
    static func decibels(_ coefficient: UInt16,
                         gainLaw: AudioSemanticMatrixSnapshot.GainLaw) -> Double? {
        guard coefficient != 0 else { return nil }
        switch gainLaw {
        case .unsignedQ214Amplitude:
            return 20 * log10(Double(coefficient) / 16_384)
        case .linearNormalized:
            return 20 * log10(Double(coefficient) / Double(UInt16.max))
        }
    }

    static func format(_ coefficient: UInt16,
                       gainLaw: AudioSemanticMatrixSnapshot.GainLaw) -> String {
        guard let value = decibels(coefficient, gainLaw: gainLaw) else { return "OFF" }
        return String(format: "%+.1f dB", value)
    }
}

private struct SaffireQ214FaderScale: View {
    private let ticks: [Double] = [12, 0, -12, -24, -36, -48, -60]

    var body: some View {
        GeometryReader { geometry in
            let travel = max(10, geometry.size.height - 28)
            ZStack(alignment: .top) {
                ForEach(ticks, id: \.self) { db in
                    Text(db == 0 ? "0" : "\(Int(db))")
                        .font(.system(size: 7).monospaced())
                        .foregroundStyle(.secondary)
                        .offset(y: 18 + (12 - db) / 72 * travel - 4)
                }
            }
            .frame(maxWidth: .infinity, alignment: .center)
        }
        .frame(width: 22)
        .accessibilityHidden(true)
    }
}
