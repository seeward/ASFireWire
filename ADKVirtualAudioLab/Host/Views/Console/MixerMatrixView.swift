import SwiftUI

// MARK: - Mixer Node Presenter (Adaptive Geometry: Strips vs Matrix Table)

struct MixerNodePresenter: View {
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var hint: MixerHintModel? {
        snap.presentation.mixerHint(for: mixer.id)
    }

    private var isSmallMixer: Bool {
        if let h = hint, h.style != ASFW_MIXER_STYLE_AUTO {
            return h.style == ASFW_MIXER_STYLE_CHANNEL_STRIPS
        }
        return mixer.crosspoints.count <= 8 && mixer.outputPortIds.count <= 2
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text(mixer.name.uppercased())
                    .font(.caption.bold())
                    .foregroundStyle(.tint)

                Text("(\(mixer.inputPortIds.count)×\(mixer.outputPortIds.count) • \(mixer.crosspoints.count) Crosspoints)")
                    .font(.caption2.monospaced())
                    .foregroundStyle(.secondary)
            }

            if isSmallMixer {
                // Small Mixer: Source Channel Cards with Send Faders
                HStack(spacing: 12) {
                    ForEach(groupedBySource(mixer)) { grp in
                        MixerSourceChannelCard(group: grp, mixer: mixer, snap: snap, state: state)
                    }
                }
            } else {
                // Large / Matrix Mixer: Elegant Matrix Grid View
                MixerMatrixTableView(mixer: mixer, snap: snap, state: state)
            }
        }
    }

    struct SourceGroup: Identifiable {
        let id: String
        let inputPortId: UInt32
        let crosspoints: [MixerCrosspointModel]
    }

    private func groupedBySource(_ mx: MixerModel) -> [SourceGroup] {
        var dict: [UInt32: [MixerCrosspointModel]] = [:]
        for cp in mx.crosspoints {
            dict[cp.inputPortId, default: []].append(cp)
        }
        return dict.keys.sorted().map { inId in
            SourceGroup(
                id: snap.portName(for: inId).replacingOccurrences(of: "Phys In: ", with: "").replacingOccurrences(of: "Host Playback: ", with: "DAW "),
                inputPortId: inId,
                crosspoints: dict[inId]!
            )
        }
    }
}

// MARK: - Mixer Matrix Table View (Scales for 12x2, 22x4, 18x16)

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

// Crosspoint cell in the Matrix Table
struct MixerCrosspointCell: View {
    let crosspoint: MixerCrosspointModel?
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        if let cp = crosspoint {
            let cpParam = snap.parameter(forTargetCrosspoint: cp.id)

            if let param = cpParam {
                VStack(spacing: 1) {
                    Slider(
                        value: Binding(
                            get: { param.scalarValue },
                            set: { state.setParameterScalar(id: param.id, value: $0) }
                        ),
                        in: param.scalarMin...param.scalarMax,
                        step: param.scalarStep
                    )
                    .frame(height: 14)

                    Text(formatMixerValue(param.scalarValue, min: param.scalarMin, max: param.scalarMax))
                        .font(.system(size: 8, weight: .bold, design: .monospaced))
                        .foregroundStyle(.tint)
                }
            } else {
                Circle()
                    .fill(Color.green)
                    .frame(width: 8, height: 8)
                    .shadow(color: Color.green.opacity(0.8), radius: 3)
            }
        } else {
            Circle()
                .fill(Color.secondary.opacity(0.15))
                .frame(width: 4, height: 4)
        }
    }

    private func formatMixerValue(_ v: Double, min: Double, max: Double) -> String {
        if v <= min { return "-∞" }
        if v >= max { return "0 dB" }
        return String(format: "%.0f", v)
    }
}

// Small Mixer Channel Card (Duet 4x2 style)
struct MixerSourceChannelCard: View {
    let group: MixerNodePresenter.SourceGroup
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: 8) {
            Text(group.id.uppercased())
                .font(.caption2.bold())
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 4)
                .background(Color.secondary.opacity(0.2))
                .clipShape(RoundedRectangle(cornerRadius: 4))

            HStack(spacing: 8) {
                ForEach(group.crosspoints) { cp in
                    let outName = snap.portName(for: cp.outputPortId)
                    let cpParam = snap.parameter(forTargetCrosspoint: cp.id)

                    VStack(spacing: 6) {
                        Text(destLabel(outName))
                            .font(.system(size: 10, weight: .bold))
                            .foregroundStyle(.secondary)

                        if let param = cpParam {
                            VerticalAudioFader(
                                value: param.scalarValue,
                                range: param.scalarMin...param.scalarMax,
                                step: param.scalarStep,
                                unit: param.unit,
                                onValueChange: { state.setParameterScalar(id: param.id, value: $0) }
                            )
                            .frame(height: 190)

                            Text(formatMixerValue(param.scalarValue, min: param.scalarMin, max: param.scalarMax))
                                .font(.system(size: 9, weight: .bold, design: .monospaced))
                                .foregroundStyle(.tint)
                        } else {
                            Circle()
                                .fill(Color.green)
                                .frame(width: 10, height: 10)
                                .frame(height: 190)
                        }
                    }
                }
            }
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(white: 0.10).opacity(0.85))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
        )
    }

    private func destLabel(_ name: String) -> String {
        if name.contains("Out L") || name.contains("Selected L") { return "➔ OUT L" }
        if name.contains("Out R") || name.contains("Selected R") { return "➔ OUT R" }
        return name
    }

    private func formatMixerValue(_ v: Double, min: Double, max: Double) -> String {
        if v <= min { return "-∞" }
        if v >= max { return "0 dB" }
        return String(format: "%.0f", v)
    }
}
