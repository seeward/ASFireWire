import SwiftUI

struct VerticalChannelStrip: View {
    let strip: ChannelStripModel
    let plan: ConsoleRowPlan
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: ConsoleMetrics.s2) {
            nameBadge
                .frame(height: ConsoleMetrics.rowBadge)

            if plan.hasSelector {
                selectorRow.frame(height: ConsoleMetrics.rowSelector)
            }
            if plan.hasPreamp {
                preampRow.frame(height: ConsoleMetrics.rowPreamp)
            }
            if plan.auxCount > 0 {
                auxRow.frame(height: plan.auxHeight)
            }
            if plan.hasPan {
                panRow.frame(height: ConsoleMetrics.rowKnob)
            }
            if plan.hasToggles {
                togglesRow.frame(height: ConsoleMetrics.rowToggles)
            }

            faderRow.frame(height: ConsoleMetrics.rowFader)
            readoutRow.frame(height: ConsoleMetrics.rowReadout)
        }
        .padding(ConsoleMetrics.s2)
        .frame(width: ConsoleMetrics.stripWidth)
        .background(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .fill(ConsoleMetrics.stripFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .strokeBorder(ConsoleMetrics.stripStroke, lineWidth: 1)
        )
    }

    // MARK: - Rows

    private var nameBadge: some View {
        Text(strip.name.uppercased())
            .font(.system(size: 10, weight: .bold, design: .monospaced))
            .lineLimit(1)
            .minimumScaleFactor(0.8)
            .foregroundStyle(accent)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(accent.opacity(0.18))
            .clipShape(RoundedRectangle(cornerRadius: ConsoleMetrics.rControl))
    }

    @ViewBuilder
    private var selectorRow: some View {
        if let mode = strip.nominalLevel {
            Menu {
                ForEach(mode.enumItems) { item in
                    Button(item.name) {
                        state.setParameterEnum(id: mode.id, value: item.value)
                    }
                }
            } label: {
                let current = mode.enumItems.first { $0.value == mode.enumValue }?.name ?? "Mode"
                HStack(spacing: 3) {
                    Text(cleanModeName(current))
                        .font(.system(size: 9, weight: .bold))
                        .lineLimit(1)
                    Image(systemName: "chevron.down")
                        .font(.system(size: 7))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color.secondary.opacity(0.18))
                .clipShape(RoundedRectangle(cornerRadius: ConsoleMetrics.rControl))
            }
            .menuStyle(.borderlessButton)
            .accessibilityLabel("\(strip.name) input mode")
        } else {
            Color.clear
        }
    }

    @ViewBuilder
    private var preampRow: some View {
        HStack(spacing: ConsoleMetrics.s1) {
            if let p48 = strip.phantom {
                ConsoleToggle(
                    glyph: "+48V",
                    label: "\(strip.name) phantom power",
                    isOn: p48.boolValue,
                    tint: .orange,
                    fontSize: 8
                ) {
                    state.setParameterBool(id: p48.id, value: !p48.boolValue)
                }
            }
            if let phase = strip.phase {
                ConsoleToggle(
                    glyph: "Ø",
                    label: "\(strip.name) phase invert",
                    isOn: phase.boolValue,
                    tint: .cyan,
                    fontSize: 9
                ) {
                    state.setParameterBool(id: phase.id, value: !phase.boolValue)
                }
            }
            if strip.phantom == nil && strip.phase == nil {
                Color.clear
            }
        }
    }

    private var auxRow: some View {
        VStack(spacing: ConsoleMetrics.s2) {
            ForEach(strip.auxSends) { aux in
                RotaryKnob(
                    title: aux.busName.replacingOccurrences(of: " Mix", with: ""),
                    value: aux.parameter.scalarValue,
                    range: aux.parameter.scalarMin...aux.parameter.scalarMax,
                    step: aux.parameter.scalarStep,
                    unit: aux.parameter.unit,
                    isBipolar: false,
                    accentColor: .blue,
                    onValueChanged: { state.setParameterScalar(id: aux.parameter.id, value: $0) }
                )
            }
            ForEach(Array(0..<max(0, plan.auxCount - strip.auxSends.count)), id: \.self) { _ in
                Color.clear.frame(height: ConsoleMetrics.rowKnob)
            }
        }
    }

    @ViewBuilder
    private var panRow: some View {
        if let pan = strip.pan {
            RotaryKnob(
                title: "PAN",
                value: pan.scalarValue,
                range: pan.scalarMin...pan.scalarMax,
                step: pan.scalarStep,
                unit: "%",
                isBipolar: true,
                accentColor: .green,
                onValueChanged: { state.setParameterScalar(id: pan.id, value: $0) }
            )
        } else {
            Color.clear
        }
    }

    @ViewBuilder
    private var togglesRow: some View {
        HStack(spacing: ConsoleMetrics.s1) {
            if let solo = strip.solo {
                ConsoleToggle(
                    glyph: "S",
                    label: "\(strip.name) solo",
                    isOn: solo.boolValue,
                    tint: .yellow
                ) {
                    state.setParameterBool(id: solo.id, value: !solo.boolValue)
                }
            }
            if let mute = strip.mute {
                ConsoleToggle(
                    glyph: "M",
                    label: "\(strip.name) mute",
                    isOn: mute.boolValue,
                    tint: .red,
                    onForeground: .white
                ) {
                    state.setParameterBool(id: mute.id, value: !mute.boolValue)
                }
            }
            if strip.solo == nil && strip.mute == nil {
                Color.clear
            }
        }
    }

    private var faderRow: some View {
        HStack(spacing: ConsoleMetrics.s1) {
            if let main = strip.mainSend {
                VerticalAudioFader(
                    value: main.parameter.scalarValue,
                    range: main.parameter.scalarMin...main.parameter.scalarMax,
                    step: main.parameter.scalarStep,
                    unit: main.parameter.unit,
                    onValueChange: { state.setParameterScalar(id: main.parameter.id, value: $0) }
                )
                .accessibilityLabel("\(strip.name) level")
            } else {
                Color.clear.frame(width: ConsoleMetrics.faderWidth)
            }

            if let meter = strip.meter {
                VerticalAudioMeter(value: meter.value, range: meter.min...meter.max, name: meter.name)
            } else {
                Color.clear.frame(width: ConsoleMetrics.meterWidth)
            }
        }
    }

    @ViewBuilder
    private var readoutRow: some View {
        if let main = strip.mainSend {
            Text(formatDb(main.parameter.scalarValue, floor: main.parameter.scalarMin))
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(main.parameter.scalarValue > 0 ? Color.red : Color.white)
        } else {
            Color.clear
        }
    }

    // MARK: - Helpers

    private var accent: Color { isPlayback ? .purple : .cyan }

    private var isPlayback: Bool {
        strip.name.contains("DAW") || strip.name.contains("Playback") || strip.name.contains("Return")
    }

    private func cleanModeName(_ name: String) -> String {
        name.replacingOccurrences(of: " (Variable Gain)", with: "")
            .replacingOccurrences(of: " (Fixed Gain)", with: "")
    }

    private func formatDb(_ value: Double, floor: Double) -> String {
        value <= floor ? "-∞" : String(format: "%.1f dB", value)
    }
}
