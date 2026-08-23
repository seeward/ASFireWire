import SwiftUI

/// Saffire's page is deliberately a single hardware view. The old page sent
/// raw TCAT application writes from Swift; this one consumes only the profile's
/// semantic matrix projection. Matrix editing follows once the driver owns its
/// matching bounded write transaction and notice choreography.
struct SaffireMixerView: View {
    private static let saffirePro24Kind: UInt32 = 0x5350_3234 // "SP24"

    let connector: ASFWDriverConnector
    @State private var matrix: AudioSemanticMatrixSnapshot?
    @State private var status = "Waiting for the Saffire Pro 24 DSP profile…"

    var body: some View {
        Group {
            if let matrix {
                SaffireSemanticMatrixView(matrix: matrix, status: status)
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
            let endpointIDs = connector.getAudioConfigurationEndpointIDs()
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
                status = "Hardware state revision \(found.stateRevision)"
            } else if endpointIDs.isEmpty {
                status = "No published Saffire audio endpoint."
            } else {
                status = "Driver is reading the hardware mixer state…"
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
}

private struct SaffireSemanticMatrixView: View {
    let matrix: AudioSemanticMatrixSnapshot
    let status: String

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                AudioTopologyCard(title: "Hardware Monitor Matrix",
                                  systemImage: "square.grid.3x3.fill",
                                  badge: "Saffire") {
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Each row is a driver-resolved mixer input; each column is a hardware mixer output.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        ScrollView([.horizontal, .vertical]) {
                            VStack(alignment: .leading, spacing: 5) {
                                HStack(spacing: 5) {
                                    Text("SOURCE →")
                                        .frame(width: 132, alignment: .leading)
                                    ForEach(matrix.outputs) { axis in
                                        Text(axisLabel(axis))
                                            .font(.caption2.monospaced().bold())
                                            .foregroundStyle(.orange)
                                            .frame(width: 54)
                                    }
                                }
                                ForEach(Array(matrix.inputs.indices), id: \.self) { inputIndex in
                                    let axis = matrix.inputs[inputIndex]
                                    HStack(spacing: 5) {
                                        Text(axisLabel(axis))
                                            .font(.caption.monospaced())
                                            .frame(width: 132, alignment: .leading)
                                        ForEach(Array(matrix.outputs.indices), id: \.self) { outputIndex in
                                            SaffireMatrixCell(
                                                value: matrix.coefficient(output: outputIndex, input: inputIndex) ?? 0,
                                                maximum: matrix.coefficientMaximum)
                                        }
                                    }
                                }
                            }
                            .padding(10)
                        }
                        .frame(minHeight: 360, maxHeight: 540)
                        Text(status)
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func axisLabel(_ axis: AudioSemanticMatrixSnapshot.Axis) -> String {
        switch axis.signalKind {
        case .analogLine: return "ANALOG \(axis.signalIndex)"
        case .hostStream: return "DAW \(axis.signalIndex)"
        case .digitalSpdif: return "S/PDIF \(axis.signalIndex)"
        case .digitalAdat: return "ADAT \(axis.signalIndex)"
        case .auxiliary: return "MIX \(axis.signalIndex)"
        default: return "SOURCE \(axis.signalIndex)"
        }
    }
}

private struct SaffireMatrixCell: View {
    let value: UInt16
    let maximum: UInt16

    private var amount: Double {
        guard maximum != 0 else { return 0 }
        return Double(value) / Double(maximum)
    }

    var body: some View {
        RoundedRectangle(cornerRadius: 4)
            .fill(Color.cyan.opacity(0.1 + amount * 0.9))
            .overlay {
                Text("\(Int((amount * 100).rounded()))")
                    .font(.system(size: 9, weight: .bold).monospaced())
                    .foregroundStyle(amount > 0.52 ? .black : .secondary)
            }
            .frame(width: 54, height: 28)
            .accessibilityLabel("Mix coefficient \(Int((amount * 100).rounded())) percent")
    }
}
