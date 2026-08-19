import SwiftUI

struct VerticalMasterStrip: View {
    let master: OutputMasterStripModel
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: 8) {
            Text(master.name.uppercased())
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(.orange)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 4)
                .background(Color.orange.opacity(0.18))
                .clipShape(RoundedRectangle(cornerRadius: 4))

            HStack(spacing: 4) {
                if let mute = master.mute {
                    Button(action: {
                        state.setParameterBool(id: mute.id, value: !mute.boolValue)
                    }) {
                        Text("MUTE")
                            .font(.system(size: 9, weight: .black))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 4)
                            .background(mute.boolValue ? Color.red : Color.secondary.opacity(0.18))
                            .foregroundStyle(mute.boolValue ? Color.white : Color.secondary)
                            .clipShape(RoundedRectangle(cornerRadius: 3))
                            .shadow(color: mute.boolValue ? Color.red.opacity(0.8) : Color.clear, radius: 3)
                    }
                    .buttonStyle(.plain)
                }

                if let dim = master.dim {
                    Button(action: {
                        state.setParameterBool(id: dim.id, value: !dim.boolValue)
                    }) {
                        Text("DIM")
                            .font(.system(size: 9, weight: .black))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 4)
                            .background(dim.boolValue ? Color.yellow : Color.secondary.opacity(0.18))
                            .foregroundStyle(dim.boolValue ? Color.black : Color.secondary)
                            .clipShape(RoundedRectangle(cornerRadius: 3))
                            .shadow(color: dim.boolValue ? Color.yellow.opacity(0.8) : Color.clear, radius: 3)
                    }
                    .buttonStyle(.plain)
                }
            }

            HStack(spacing: 4) {
                if let vol = master.level {
                    VerticalAudioFader(
                        value: vol.scalarValue,
                        range: vol.scalarMin...vol.scalarMax,
                        step: vol.scalarStep,
                        unit: vol.unit,
                        onValueChange: { state.setParameterScalar(id: vol.id, value: $0) }
                    )
                }

                HStack(spacing: 2) {
                    ForEach(master.meters.prefix(2)) { m in
                        VerticalAudioMeter(value: m.value, range: m.min...m.max, name: m.name)
                    }
                }
            }
            .frame(height: 180)

            if let vol = master.level {
                VStack(spacing: 1) {
                    Text(vol.scalarValue <= vol.scalarMin ? "-∞" : String(format: "%.1f dB", vol.scalarValue))
                        .font(.system(size: 10, weight: .bold, design: .monospaced))
                        .foregroundStyle(.orange)
                }
            }
        }
        .padding(8)
        .frame(width: 96)
        .background(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(Color(white: 0.13).opacity(0.95))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(Color.orange.opacity(0.3), lineWidth: 1)
        )
    }
}
