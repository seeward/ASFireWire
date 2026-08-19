import SwiftUI

struct VerticalMasterStrip: View {
    let parameters: [ParameterModel]
    let meters: [MeterModel]
    @ObservedObject var state: VirtualLabState

    private var volumeParam: ParameterModel? {
        parameters.first { $0.kind == ASFW_PARAM_KIND_SCALAR }
    }

    private var muteParam: ParameterModel? {
        parameters.first {
            $0.kind == ASFW_PARAM_KIND_BOOLEAN &&
            ($0.semantic == ASFW_SEMANTIC_MUTE || $0.name.lowercased().contains("mute"))
        }
    }

    private var dimParam: ParameterModel? {
        parameters.first {
            $0.kind == ASFW_PARAM_KIND_BOOLEAN &&
            ($0.semantic == ASFW_SEMANTIC_DIM || $0.name.lowercased().contains("dim"))
        }
    }

    var body: some View {
        VStack(spacing: 10) {
            Text("MAIN OUT")
                .font(.caption.bold())
                .foregroundStyle(.orange)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .background(Color.orange.opacity(0.15))
                .clipShape(RoundedRectangle(cornerRadius: 5))

            HStack(spacing: 6) {
                if let mute = muteParam {
                    Button(action: {
                        state.setParameterBool(id: mute.id, value: !mute.boolValue)
                    }) {
                        Text("MUTE")
                            .font(.system(size: 10, weight: .black))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 6)
                            .background(mute.boolValue ? Color.red : Color.secondary.opacity(0.18))
                            .foregroundStyle(mute.boolValue ? Color.white : Color.secondary)
                            .clipShape(RoundedRectangle(cornerRadius: 4))
                            .shadow(color: mute.boolValue ? Color.red.opacity(0.8) : Color.clear, radius: 4)
                    }
                    .buttonStyle(.plain)
                }

                if let dim = dimParam {
                    Button(action: {
                        state.setParameterBool(id: dim.id, value: !dim.boolValue)
                    }) {
                        Text("DIM")
                            .font(.system(size: 10, weight: .black))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 6)
                            .background(dim.boolValue ? Color.yellow : Color.secondary.opacity(0.18))
                            .foregroundStyle(dim.boolValue ? Color.black : Color.secondary)
                            .clipShape(RoundedRectangle(cornerRadius: 4))
                            .shadow(color: dim.boolValue ? Color.yellow.opacity(0.8) : Color.clear, radius: 4)
                    }
                    .buttonStyle(.plain)
                }
            }

            HStack(spacing: 6) {
                if let vol = volumeParam {
                    VerticalAudioFader(
                        value: vol.scalarValue,
                        range: vol.scalarMin...vol.scalarMax,
                        step: vol.scalarStep,
                        unit: vol.unit,
                        onValueChange: { state.setParameterScalar(id: vol.id, value: $0) }
                    )
                }

                HStack(spacing: 3) {
                    ForEach(meters.prefix(2)) { m in
                        VerticalAudioMeter(value: m.value, range: m.min...m.max, name: m.name)
                    }
                }
            }
            .frame(height: 200)

            if let vol = volumeParam {
                VStack(spacing: 1) {
                    Text(String(format: "%.1f dB", vol.scalarValue))
                        .font(.subheadline.monospaced().bold())
                        .foregroundStyle(.orange)
                    Text("MASTER LEVEL")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(10)
        .frame(width: 140)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(white: 0.14).opacity(0.95))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.orange.opacity(0.35), lineWidth: 1.5)
        )
    }
}
