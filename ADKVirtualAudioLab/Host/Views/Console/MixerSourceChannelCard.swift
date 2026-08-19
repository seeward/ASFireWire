import SwiftUI

struct MixerSourceChannelCard: View {
    let group: MixerNodePresenter.SourceGroup
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: 8) {
            Text(group.id.uppercased())
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(.tint)
                .lineLimit(1)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 3)
                .background(Color.secondary.opacity(0.12))
                .clipShape(RoundedRectangle(cornerRadius: 4))

            // Sends to each mixer output bus
            HStack(spacing: 6) {
                ForEach(group.crosspoints) { cp in
                    let param = snap.parameter(forTargetCrosspoint: cp.id)
                    let outName = snap.portName(for: cp.outputPortId)
                    let label = outName.contains("L") ? "SEND L" : (outName.contains("R") ? "SEND R" : "BUS")

                    VStack(spacing: 4) {
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
                            .frame(height: 180)

                            Text(formatDb(p.scalarValue))
                                .font(.system(size: 8, weight: .bold, design: .monospaced))
                                .foregroundStyle(p.scalarValue > p.scalarMin ? Color.cyan : Color.secondary)
                        } else {
                            Rectangle()
                                .fill(Color.secondary.opacity(0.05))
                                .frame(width: 48, height: 180)
                        }
                    }
                }
            }
        }
        .padding(8)
        .background(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(Color(white: 0.12).opacity(0.8))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
        )
    }

    private func formatDb(_ v: Double) -> String {
        if v <= -90.0 { return "-∞" }
        return String(format: "%.1f", v)
    }
}
