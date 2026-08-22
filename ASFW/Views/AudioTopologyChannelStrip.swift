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
    let isControlled: Bool
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
    let toggleControl: () -> Void

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
        AudioConsoleStripShell(
            title: strip.name,
            tint: tint,
            width: Metrics.width,
            borderTint: isSoloed ? .yellow : isControlled ? .teal : nil,
            isDimmed: isSuppressed
        ) {
            VStack(spacing: 6) {

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

            // `ctrl` assigns this strip to the front-panel assignable knob.
                VStack(spacing: 4) {
                    HStack(spacing: 4) {
                        toggle("ctrl", isOn: isControlled, tint: .teal, action: toggleControl)
                        toggle("link", isOn: isLinked, tint: .blue, action: toggleLink)
                    }
                    HStack(spacing: 4) {
                        toggle("mute", isOn: isMuted, tint: .red, action: toggleMute)
                        if strip.kind.isInput {
                            toggle("solo", isOn: isSoloed, tint: .yellow, action: toggleSolo)
                        }
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
        }
    }

    private var peakCaption: String {
        let held = strip.channels.compactMap { $0.meterIndex.map { peakHold.peak(at: $0) } }
        guard let loudest = held.max() else { return "" }
        return MAudio1814Level.formatMeter(raw: loudest)
    }

    private func fader(_ channel: AudioTopologyStripChannel) -> some View {
        AudioTopologyFader(
            title: "\(strip.name) \(channel.label) level",
            position: MAudio1814Level.position(raw: level(channel)),
            tint: tint,
            isEnabled: !isSuppressed,
            onChanged: { setLevel(channel, $0) }
        )
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
        AudioConsoleStripToggle(
            title: title,
            accessibilityLabel: "\(strip.name) \(title)",
            isOn: isOn,
            isEnabled: true,
            tint: tint,
            action: action
        )
    }
}
