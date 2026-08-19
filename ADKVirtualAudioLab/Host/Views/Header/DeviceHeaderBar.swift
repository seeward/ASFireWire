import SwiftUI

struct DeviceHeaderBar: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        HStack(alignment: .center, spacing: 16) {
            HStack(spacing: 12) {
                Circle()
                    .fill(Color.green)
                    .frame(width: 10, height: 10)
                    .shadow(color: .green.opacity(0.9), radius: 5)

                VStack(alignment: .leading, spacing: 3) {
                    Text("\(snap.manufacturer) \(snap.model)")
                        .font(.title2)
                        .fontWeight(.bold)

                    HStack(spacing: 8) {
                        Text("REVISION \(snap.revision)")
                            .font(.caption.monospaced().bold())
                            .foregroundStyle(.tint)

                        Text("•")
                            .foregroundStyle(.secondary)

                        Text("NODE-DRIVEN TOPOLOGY PROJECTION")
                            .font(.caption2.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            }

            Spacer()

            Picker("Device Model", selection: Binding(
                get: { snap.deviceKind },
                set: { state.selectDevice($0) }
            )) {
                Text("Apogee Duet").tag(ASFW_VIRTUAL_DEVICE_DUET)
                Text("TerraTec PHASE 88").tag(ASFW_VIRTUAL_DEVICE_PHASE88)
                Text("M-Audio FW1814").tag(ASFW_VIRTUAL_DEVICE_FW1814)
                Text("Saffire Pro 24 DSP").tag(ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP)
            }
            .pickerStyle(.segmented)
            .frame(width: 480)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.85))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Color.white.opacity(0.1), lineWidth: 1)
        )
    }
}
