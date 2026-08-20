import SwiftUI

struct MixerSourceChannelCard: View {
    let group: MixerNodePresenter.SourceGroup
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: ConsoleMetrics.s2) {
            Text(group.id.uppercased())
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(.tint)
                .lineLimit(1)
                .frame(maxWidth: .infinity)
                .padding(.vertical, ConsoleMetrics.s1)
                .background(Color.secondary.opacity(0.12))
                .clipShape(RoundedRectangle(cornerRadius: ConsoleMetrics.rControl))

            // Sends to each mixer output bus
            HStack(spacing: ConsoleMetrics.s2) {
                ForEach(group.crosspoints) { cp in
                    let param = snap.parameter(forTargetCrosspoint: cp.id)
                    let outName = snap.portName(for: cp.outputPortId)
                    let label = outName.contains("L") ? "SEND L" : (outName.contains("R") ? "SEND R" : "BUS")

                    VStack(spacing: ConsoleMetrics.s1) {
                        Text(label)
                            .font(.system(size: 8, weight: .bold))
                            .foregroundStyle(.secondary)

                        if let p = param {
                            VerticalAudioFader(
                                value: p.scalarValue,
                                range: p.scalarMin...p.scalarMax,
                                step: p.scalarStep,
                                unit: p.unit,
                                onValueChange: { state.setParameterScalar(id: p.id, value: $0) }
                            )
                            .frame(height: ConsoleMetrics.rowFader)

                            Text(formatDb(p.scalarValue))
                                .font(.system(size: 8, weight: .bold, design: .monospaced))
                                .foregroundStyle(p.scalarValue > p.scalarMin ? Color.cyan : Color.secondary)
                        } else {
                            Rectangle()
                                .fill(Color.secondary.opacity(0.05))
                                .frame(width: ConsoleMetrics.faderWidth, height: ConsoleMetrics.rowFader)
                        }
                    }
                }
            }
        }
        .padding(ConsoleMetrics.s2)
        .background(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .fill(ConsoleMetrics.stripFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .strokeBorder(ConsoleMetrics.stripStroke, lineWidth: 1)
        )
    }

    private func formatDb(_ v: Double) -> String {
        if v <= -90.0 { return "-∞" }
        return String(format: "%.1f", v)
    }
}
