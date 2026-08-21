import SwiftUI

struct AudioTopologyChannelStrip: View {
    let channel: AudioTopologyChannel
    let meters: AudioMeterSnapshot?
    let setSend: (AudioTopologySend, Bool) -> Void
    /// Applies one percentage to every register in a pair. Each is a separate
    /// hardware write; the control plane coalesces and paces them.
    let setLevel: ([MAudio1814ControlID], Double) -> Void
    let setWidth: ([MAudio1814ControlID], Double) -> Void

    private var tint: Color { channel.kind == .playback ? .purple : .cyan }

    var body: some View {
        VStack(spacing: 6) {
            Text(channel.name)
                .font(.caption.monospaced().bold())
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .foregroundStyle(tint)
                .frame(maxWidth: .infinity, minHeight: 32)
                .background(tint.opacity(0.18))
                .clipShape(RoundedRectangle(cornerRadius: 4))

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

            AudioTopologyRotary(title: "AUX", value: channel.auxSend, tint: .yellow) {
                setLevel(channel.auxControls, $0)
            }

            if !channel.widthControls.isEmpty {
                AudioTopologyRotary(title: "WIDTH", value: channel.width, tint: .teal) {
                    setWidth(channel.widthControls, $0)
                }
            }

            HStack(spacing: 4) {
                AudioTopologyFader(value: channel.gain) { setLevel(channel.gainControls, $0) }
                    .frame(height: 150)
                AudioTopologyMeter(pair: channel.meterPair, snapshot: meters, name: channel.name)
                    .frame(height: 150)
            }

            Text("\(Int(channel.gain.rounded()))%")
                .font(.caption.monospaced().bold())
                .foregroundStyle(tint)
        }
        .padding(8)
        .frame(width: 100, height: 420)
        .background(AudioTopologyStripBackground(stroke: tint))
    }
}

/// A compact horizontal control for the secondary per-channel parameters, so a
/// strip can carry three continuous values without becoming three faders tall.
struct AudioTopologyRotary: View {
    let title: String
    let value: Double
    let tint: Color
    let onChanged: (Double) -> Void
    @State private var dragStartValue: Double?

    var body: some View {
        VStack(spacing: 2) {
            HStack {
                Text(title).font(.system(size: 8).monospaced()).foregroundStyle(.secondary)
                Spacer()
                Text("\(Int(value.rounded()))").font(.system(size: 8).monospaced()).foregroundStyle(tint)
            }
            GeometryReader { geometry in
                let width = max(1, geometry.size.width)
                ZStack(alignment: .leading) {
                    Capsule().fill(Color(white: 0.05))
                    Capsule().fill(tint.opacity(0.75))
                        .frame(width: width * value / 100)
                }
                .frame(height: 6)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let start = dragStartValue ?? value
                        dragStartValue = start
                        onChanged(max(0, min(100,
                            start + Double(gesture.translation.width) / Double(width) * 100)))
                    }
                    .onEnded { _ in dragStartValue = nil })
            }
            .frame(height: 8)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(title)
        .accessibilityValue("\(Int(value.rounded())) percent")
        .accessibilityAdjustableAction { direction in
            onChanged(max(0, min(100, value + (direction == .increment ? 5 : -5))))
        }
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
