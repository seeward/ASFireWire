import SwiftUI

struct DextManagementView: View {
    @StateObject private var manager = ExtensionManager()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("ADKVirtualAudioLab Host")
                .font(.title2)
            Text("Dext: \(ExtensionManager.dextIdentifier)")
                .font(.caption)
                .textSelection(.enabled)

            HStack(spacing: 12) {
                Button("Activate") { manager.activate() }
                Button("Deactivate") { manager.deactivate() }
            }

            Text(manager.status)
                .font(.callout)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            Text("After activation, the virtual device appears in Audio MIDI Setup. Start playback at it, then stop — the dext dumps verifier and O/C counters at StopIO (see BENCH.md). The inspector below snapshots recent packets on demand (⌘D) without touching the streaming path.")
                .font(.caption)
                .foregroundStyle(.tertiary)

            Divider()

            PacketInspectorView()
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .padding(20)
    }
}
