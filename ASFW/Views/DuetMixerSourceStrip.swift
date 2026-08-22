import SwiftUI

struct DuetMixerSourceStrip: View {
    let source: DuetConsoleSnapshot.MixerStrip
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        AudioConsoleStripShell(
            title: source.name.uppercased(),
            tint: source.name.contains("DAW") ? .purple : .cyan,
            width: 196,
            borderTint: nil
        ) {
            AudioConsoleStripSlots(controlHeight: 92, faderHeight: 220) {
                Text("CUE SENDS")
                    .font(.caption2.monospaced().bold())
                    .foregroundStyle(.secondary)
            } faderBlock: {
                DuetMixerSourceControls(source: source, isWriting: isWriting, submit: submit)
            }
        }
    }
}
