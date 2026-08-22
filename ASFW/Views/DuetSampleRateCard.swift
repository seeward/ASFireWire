import SwiftUI

/// Duet's rate selector intentionally exposes no clock-source or optical
/// controls: the hardware protocol offers neither in this configuration path.
struct DuetSampleRateControl: View {
    let committedRateHz: UInt32
    @Binding var selectedRateHz: UInt32
    let isApplying: Bool
    let apply: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            Label("Sample Rate", systemImage: "waveform")
                .font(.headline)
            Picker("Sample rate", selection: $selectedRateHz) {
                Text("44.1 kHz").tag(UInt32(44_100))
                Text("48 kHz").tag(UInt32(48_000))
            }
            .labelsHidden()
            .pickerStyle(.segmented)
            .frame(width: 190)
            Button(isApplying ? "Applying…" : "Apply", action: apply)
                .disabled(isApplying || selectedRateHz == committedRateHz)
        }
    }
}
