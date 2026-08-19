import SwiftUI

struct MixerMatrixTableView: View {
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            // Table Header: Output Busses
            HStack(spacing: 4) {
                Text("SOURCE / BUS")
                    .font(.system(size: 9, weight: .bold))
                    .foregroundStyle(.secondary)
                    .frame(width: 140, alignment: .leading)

                ForEach(mixer.outputPortIds, id: \.self) { outPortId in
                    let outName = cleanBusName(snap.portName(for: outPortId))
                    let outParams = snap.parameters(forTargetPort: outPortId)

                    VStack(spacing: 2) {
                        Text(outName)
                            .font(.system(size: 9, weight: .bold))
                            .foregroundStyle(.orange)
                            .lineLimit(1)

                        if let vol = outParams.first(where: { $0.kind == ASFW_PARAM_KIND_SCALAR }) {
                            Text(String(format: "%.0f dB", vol.scalarValue))
                                .font(.system(size: 8, weight: .bold, design: .monospaced))
                                .foregroundStyle(.tint)
                        }
                    }
                    .frame(width: 68)
                }
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Color.secondary.opacity(0.12))
            .clipShape(RoundedRectangle(cornerRadius: 6))

            // Table Rows: Input Sources
            ScrollView(.vertical, showsIndicators: true) {
                VStack(spacing: 3) {
                    ForEach(mixer.inputPortIds, id: \.self) { inPortId in
                        let inName = cleanSourceName(snap.portName(for: inPortId))
                        let inParams = snap.parameters(forTargetPort: inPortId)

                        HStack(spacing: 4) {
                            HStack(spacing: 4) {
                                Text(inName)
                                    .font(.system(size: 9, weight: .medium))
                                    .lineLimit(1)
                                Spacer()

                                if let mute = inParams.first(where: { $0.kind == ASFW_PARAM_KIND_BOOLEAN }) {
                                    Button(action: {
                                        state.setParameterBool(id: mute.id, value: !mute.boolValue)
                                    }) {
                                        Text("M")
                                            .font(.system(size: 8, weight: .bold))
                                            .padding(.horizontal, 4)
                                            .padding(.vertical, 2)
                                            .background(mute.boolValue ? Color.red : Color.secondary.opacity(0.2))
                                            .foregroundStyle(mute.boolValue ? Color.white : Color.secondary)
                                            .clipShape(RoundedRectangle(cornerRadius: 2))
                                    }
                                    .buttonStyle(.plain)
                                }
                            }
                            .frame(width: 140, alignment: .leading)

                            ForEach(mixer.outputPortIds, id: \.self) { outPortId in
                                let cp = mixer.crosspoints.first { $0.inputPortId == inPortId && $0.outputPortId == outPortId }
                                MixerCrosspointCell(crosspoint: cp, snap: snap, state: state)
                                    .frame(width: 68)
                            }
                        }
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(Color.secondary.opacity(0.03))
                        .clipShape(RoundedRectangle(cornerRadius: 4))
                    }
                }
            }
            .frame(maxHeight: 280)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(white: 0.11).opacity(0.9))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
        )
    }

    private func cleanSourceName(_ n: String) -> String {
        return n.replacingOccurrences(of: "Mixer In: ", with: "")
                .replacingOccurrences(of: "Phys In: ", with: "")
                .replacingOccurrences(of: "Host Playback: ", with: "DAW ")
    }

    private func cleanBusName(_ n: String) -> String {
        return n.replacingOccurrences(of: "Mixer Out: ", with: "BUS ")
                .replacingOccurrences(of: "Mixer Out ", with: "BUS ")
    }
}
