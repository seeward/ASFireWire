import SwiftUI

struct VerticalChannelStrip: View {
    let title: String
    let params: [ParameterModel]
    let meter: MeterModel?
    @ObservedObject var state: VirtualLabState

    private var gainParam: ParameterModel? {
        params.first { $0.kind == ASFW_PARAM_KIND_SCALAR }
    }

    private var toggleParams: [ParameterModel] {
        params.filter { $0.kind == ASFW_PARAM_KIND_BOOLEAN }
    }

    private var enumParam: ParameterModel? {
        params.first { $0.kind == ASFW_PARAM_KIND_ENUM }
    }

    var body: some View {
        VStack(spacing: 10) {
            Text(title.uppercased())
                .font(.caption.bold())
                .foregroundStyle(.cyan)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .background(Color.cyan.opacity(0.15))
                .clipShape(RoundedRectangle(cornerRadius: 5))

            if let mode = enumParam {
                Menu {
                    ForEach(mode.enumItems) { item in
                        Button(item.name) {
                            state.setParameterEnum(id: mode.id, value: item.value)
                        }
                    }
                } label: {
                    let currentName = mode.enumItems.first(where: { $0.value == mode.enumValue })?.name ?? "Mode"
                    HStack(spacing: 4) {
                        Text(cleanModeName(currentName))
                            .font(.caption2.bold())
                            .lineLimit(1)
                        Image(systemName: "chevron.down")
                            .font(.system(size: 8))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 4)
                    .background(Color.secondary.opacity(0.18))
                    .clipShape(RoundedRectangle(cornerRadius: 5))
                }
                .menuStyle(.borderlessButton)
            }

            HStack(spacing: 6) {
                ForEach(toggleParams) { t in
                    let is48V = t.semantic == ASFW_SEMANTIC_PHANTOM_POWER || t.name.contains("+48V")
                    let isPhase = t.semantic == ASFW_SEMANTIC_PHASE_INVERT || t.name.contains("Phase")
                    let label = is48V ? "+48V" : (isPhase ? "Ø Phase" : "SW")

                    Button(action: {
                        state.setParameterBool(id: t.id, value: !t.boolValue)
                    }) {
                        Text(label)
                            .font(.system(size: 10, weight: .bold))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 6)
                            .background(
                                t.boolValue
                                    ? (is48V ? Color.orange : Color.cyan)
                                    : Color.secondary.opacity(0.15)
                            )
                            .foregroundStyle(t.boolValue ? Color.black : Color.secondary)
                            .clipShape(RoundedRectangle(cornerRadius: 4))
                            .shadow(color: t.boolValue ? (is48V ? .orange.opacity(0.6) : .cyan.opacity(0.6)) : .clear, radius: 3)
                    }
                    .buttonStyle(.plain)
                }
            }

            HStack(spacing: 6) {
                if let gain = gainParam {
                    VerticalAudioFader(
                        value: gain.scalarValue,
                        range: gain.scalarMin...gain.scalarMax,
                        step: gain.scalarStep,
                        unit: gain.unit,
                        onValueChange: { state.setParameterScalar(id: gain.id, value: $0) }
                    )
                }

                if let m = meter {
                    VerticalAudioMeter(value: m.value, range: m.min...m.max, name: m.name)
                }
            }
            .frame(height: 200)

            if let gain = gainParam {
                VStack(spacing: 1) {
                    Text(String(format: "+%.1f dB", gain.scalarValue))
                        .font(.subheadline.monospaced().bold())
                        .foregroundStyle(.tint)
                    Text("PREAMP GAIN")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(10)
        .frame(width: 128)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(white: 0.12).opacity(0.9))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.white.opacity(0.1), lineWidth: 1)
        )
    }

    private func cleanModeName(_ n: String) -> String {
        return n.replacingOccurrences(of: " (Variable Gain)", with: "")
                .replacingOccurrences(of: " (Fixed Gain)", with: "")
    }
}
