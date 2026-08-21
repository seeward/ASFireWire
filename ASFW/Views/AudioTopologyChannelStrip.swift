import SwiftUI

struct AudioTopologyChannelStrip: View {
    let channel: AudioTopologyChannel
    let meters: AudioMeterSnapshot?
    let setSend: (AudioTopologySend, Bool) -> Void

    var body: some View {
        VStack(spacing: 8) {
            Text(channel.name)
                .font(.caption.monospaced().bold())
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .foregroundStyle(channel.kind == .playback ? .purple : .cyan)
                .frame(maxWidth: .infinity, minHeight: 32)
                .background((channel.kind == .playback ? Color.purple : .cyan).opacity(0.18))
                .clipShape(RoundedRectangle(cornerRadius: 4))
            Spacer(minLength: 12)
            HStack(spacing: 4) {
                ForEach(channel.sendControls) { send in
                    Button(send.label) { setSend(send, !send.isEnabled) }
                        .buttonStyle(.plain)
                        .font(.caption.bold())
                        .frame(maxWidth: .infinity, minHeight: 22)
                        .background(send.isEnabled ? Color.green : Color.secondary.opacity(0.18))
                        .foregroundStyle(send.isEnabled ? Color.black : Color.secondary)
                        .clipShape(RoundedRectangle(cornerRadius: 4))
                        .accessibilityLabel("\(channel.name) send to \(send.label)")
                }
            }
            Spacer(minLength: 16)
            AudioTopologyMeter(pair: channel.meterPair, snapshot: meters, name: channel.name)
                .frame(height: 180)
            Text("SEND")
                .font(.caption2.monospaced()).foregroundStyle(.secondary)
        }
        .padding(8)
        .frame(width: 92, height: 330)
        .background(AudioTopologyStripBackground(stroke: channel.kind == .playback ? .purple : .cyan))
    }
}

struct AudioTopologyStripBackground: View {
    let stroke: Color

    var body: some View {
        RoundedRectangle(cornerRadius: 8)
            .fill(Color(white: 0.11))
            .overlay { RoundedRectangle(cornerRadius: 8).strokeBorder(stroke.opacity(0.3), lineWidth: 1) }
    }
}
