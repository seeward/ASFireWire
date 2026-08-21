import SwiftUI

/// One console strip.
///
/// The rows are fixed-height slots, empty where a position in the signal flow
/// has no such control, so faders and meters line up straight across the whole
/// rack. A console that does not align is unreadable at a glance, which is the
/// only thing a console is for.
struct AudioTopologyChannelStrip: View {
    let strip: AudioTopologyStrip
    let meters: AudioMeterSnapshot?
    let peakHold: AudioMeterPeakHold
    let isLinked: Bool
    let isMuted: Bool
    let isSoloed: Bool
    let isSuppressed: Bool
    let level: (AudioTopologyStripChannel) -> Int32
    let setLevel: (AudioTopologyStripChannel, Double) -> Void
    let setPan: (AudioTopologyStripChannel, Double) -> Void
    let setAux: (AudioTopologyStripChannel, Double) -> Void
    let setSend: (AudioTopologySend, Bool) -> Void
    let setSource: (MAudio1814ControlID, Int32) -> Void
    let toggleLink: () -> Void
    let toggleMute: () -> Void
    let toggleSolo: () -> Void

    private enum Metrics {
        static let width: CGFloat = 138
        static let knobRow: CGFloat = 38
        static let faderBlock: CGFloat = 190
        static let buttonRow: CGFloat = 34
    }

    private var tint: Color {
        switch strip.kind {
        case .physicalInput: return .cyan
        case .playback: return .purple
        case .output: return .orange
        case .aux: return .yellow
        case .headphone: return .pink
        }
    }

    private var hasPan: Bool { strip.channels.contains { $0.panControl != nil } }
    private var hasAux: Bool { strip.channels.contains { $0.auxControl != nil } }

    var body: some View {
        VStack(spacing: 6) {
            Text(strip.name)
                .font(.system(size: 10, weight: .bold).monospaced())
                .lineLimit(1).minimumScaleFactor(0.65)
                .foregroundStyle(tint)
                .frame(maxWidth: .infinity, minHeight: 24)
                .background(tint.opacity(0.2))
                .clipShape(RoundedRectangle(cornerRadius: 4))

            knobSlot("aux", present: hasAux) { channel in
                AudioTopologyKnob(
                    value: MAudio1814Level.position(raw: channel.auxRaw),
                    tint: .yellow,
                    caption: MAudio1814Level.format(raw: channel.auxRaw)
                ) { setAux(channel, $0) }
            }

            knobSlot("pan", present: hasPan) { channel in
                AudioTopologyKnob(
                    value: (MAudio1814Level.panPosition(raw: channel.panRaw) + 1) / 2,
                    tint: .blue,
                    caption: MAudio1814Level.formatPan(raw: channel.panRaw),
                    isBipolar: true
                ) { setPan(channel, $0 * 2 - 1) }
            }

            // Faders flank the shared dB scale; meters sit alongside at the same
            // height, so a level and what it is doing read as one gesture.
            HStack(alignment: .center, spacing: 3) {
                fader(strip.channels[0])
                AudioTopologyFaderScale()
                fader(strip.channels.count > 1 ? strip.channels[1] : strip.channels[0])
                Spacer(minLength: 2)
                ForEach(strip.channels) { channel in
                    AudioTopologyMeter(
                        index: channel.meterIndex, snapshot: meters, peakHold: peakHold,
                        name: "\(strip.name) \(channel.label)")
                }
            }
            .frame(height: Metrics.faderBlock)

            HStack(spacing: 2) {
                ForEach(strip.channels) { channel in
                    Text(MAudio1814Level.format(raw: level(channel)))
                        .font(.system(size: 10, weight: .semibold).monospaced())
                        .foregroundStyle(isSuppressed ? .secondary : tint)
                        .frame(maxWidth: .infinity)
                }
                Text(peakCaption)
                    .font(.system(size: 9).monospaced())
                    .foregroundStyle(.secondary)
                    .frame(width: 30)
            }

            HStack(spacing: 4) {
                toggle("link", isOn: isLinked, tint: .blue, action: toggleLink)
                toggle("mute", isOn: isMuted, tint: .red, action: toggleMute)
                if strip.kind.isInput {
                    toggle("solo", isOn: isSoloed, tint: .yellow, action: toggleSolo)
                }
            }

            buttonSlot("out", choices: strip.sends.map { send in
                (send.label, send.isEnabled, { setSend(send, !send.isEnabled) })
            })

            buttonSlot(strip.source?.name.lowercased() ?? "",
                       choices: (strip.source?.choices ?? []).map { choice in
                           (choice.name, strip.source?.selectedValue == choice.value, {
                               if let control = strip.source?.control {
                                   setSource(control, choice.value)
                               }
                           })
                       })

            Spacer(minLength: 0)
        }
        .padding(8)
        .frame(width: Metrics.width)
        .frame(maxHeight: .infinity, alignment: .top)
        .background(
            RoundedRectangle(cornerRadius: 9)
                .fill(Color(white: 0.13))
                .overlay {
                    RoundedRectangle(cornerRadius: 9)
                        .strokeBorder(isSoloed ? Color.yellow : tint.opacity(0.28),
                                      lineWidth: isSoloed ? 2 : 1)
                }
        )
        .opacity(isSuppressed ? 0.5 : 1)
    }

    private var peakCaption: String {
        let held = strip.channels.compactMap { $0.meterIndex.map { peakHold.peak(at: $0) } }
        guard let loudest = held.max() else { return "" }
        return MAudio1814Level.formatMeter(raw: loudest)
    }

    private func fader(_ channel: AudioTopologyStripChannel) -> some View {
        AudioTopologyFader(
            position: MAudio1814Level.position(raw: level(channel)),
            tint: tint,
            isEnabled: !isSuppressed
        ) { setLevel(channel, $0) }
    }

    /// A fixed-height row of one knob per channel. Reserved even when this strip
    /// has no such control, so every fader in the rack starts at the same y.
    @ViewBuilder
    private func knobSlot(_ caption: String, present: Bool,
                          @ViewBuilder content: @escaping (AudioTopologyStripChannel) -> some View)
        -> some View {
        VStack(spacing: 1) {
            if present {
                Text(caption)
                    .font(.system(size: 8).monospaced())
                    .foregroundStyle(.secondary)
                HStack(spacing: 10) {
                    ForEach(strip.channels) { channel in content(channel) }
                }
            }
        }
        .frame(height: Metrics.knobRow)
    }

    @ViewBuilder
    private func buttonSlot(_ caption: String,
                            choices: [(String, Bool, () -> Void)]) -> some View {
        VStack(spacing: 2) {
            if !choices.isEmpty {
                Text(caption)
                    .font(.system(size: 8).monospaced())
                    .foregroundStyle(.secondary)
                HStack(spacing: 4) {
                    ForEach(Array(choices.enumerated()), id: \.offset) { _, choice in
                        toggle(choice.0, isOn: choice.1, tint: .blue, action: choice.2)
                    }
                }
            }
        }
        .frame(height: Metrics.buttonRow)
    }

    private func toggle(_ title: String, isOn: Bool, tint: Color,
                        action: @escaping () -> Void) -> some View {
        Button(title, action: action)
            .buttonStyle(.plain)
            .font(.system(size: 10, weight: .bold))
            .frame(maxWidth: .infinity, minHeight: 20)
            .background(isOn ? tint : Color.white.opacity(0.08))
            .foregroundStyle(isOn ? Color.black : Color.secondary)
            .clipShape(RoundedRectangle(cornerRadius: 4))
    }
}

/// A rotary. Bipolar controls fill outward from the top centre so a centred pan
/// is visibly centred; unipolar ones fill from the minimum.
struct AudioTopologyKnob: View {
    let value: Double
    let tint: Color
    let caption: String
    var isBipolar = false
    let onChanged: (Double) -> Void

    @State private var dragStart: Double?
    @State private var live: Double?

    private var shown: Double { live ?? value }

    var body: some View {
        VStack(spacing: 1) {
            ZStack {
                Circle().stroke(Color(white: 0.28), lineWidth: 2.5)
                Circle()
                    .trim(from: isBipolar ? min(0.375, shown * 0.75) : 0,
                          to: isBipolar ? max(0.375, shown * 0.75) : max(0.004, shown * 0.75))
                    .stroke(tint, style: StrokeStyle(lineWidth: 2.5, lineCap: .butt))
                    .rotationEffect(.degrees(135))
                Rectangle()
                    .fill(Color.white)
                    .frame(width: 2, height: 9)
                    .offset(y: -5)
                    .rotationEffect(.degrees(-135 + shown * 270))
            }
            .frame(width: 26, height: 26)
            Text(caption)
                .font(.system(size: 8).monospaced())
                .foregroundStyle(.secondary)
                .lineLimit(1).minimumScaleFactor(0.6)
        }
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { gesture in
                let start = dragStart ?? shown
                dragStart = start
                let next = max(0, min(1, start - Double(gesture.translation.height) / 140))
                live = next
                onChanged(next)
            }
            .onEnded { _ in
                dragStart = nil
                live = nil
            })
        .accessibilityElement(children: .ignore)
        .accessibilityValue(caption)
        .accessibilityAdjustableAction { direction in
            onChanged(max(0, min(1, value + (direction == .increment ? 0.05 : -0.05))))
        }
    }
}
