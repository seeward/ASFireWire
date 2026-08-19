import SwiftUI

struct VerticalChannelStrip: View {
    let strip: ChannelStripModel
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: 8) {
            // 1. Channel Header Badge
            Text(strip.name.uppercased())
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(isPlayback ? .purple : .cyan)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 4)
                .background((isPlayback ? Color.purple : Color.cyan).opacity(0.18))
                .clipShape(RoundedRectangle(cornerRadius: 4))

            // 2. Mode / Nominal Level Selector (if present)
            if let mode = strip.nominalLevel {
                Menu {
                    ForEach(mode.enumItems) { item in
                        Button(item.name) {
                            state.setParameterEnum(id: mode.id, value: item.value)
                        }
                    }
                } label: {
                    let currentName = mode.enumItems.first(where: { $0.value == mode.enumValue })?.name ?? "Mode"
                    HStack(spacing: 3) {
                        Text(cleanModeName(currentName))
                            .font(.system(size: 9, weight: .bold))
                            .lineLimit(1)
                        Image(systemName: "chevron.down")
                            .font(.system(size: 7))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 3)
                    .background(Color.secondary.opacity(0.18))
                    .clipShape(RoundedRectangle(cornerRadius: 4))
                }
                .menuStyle(.borderlessButton)
            }

            // 3. Preamp Switches: +48V / Phase
            if strip.phantom != nil || strip.phase != nil {
                HStack(spacing: 4) {
                    if let p48 = strip.phantom {
                        Button(action: {
                            state.setParameterBool(id: p48.id, value: !p48.boolValue)
                        }) {
                            Text("+48V")
                                .font(.system(size: 8, weight: .bold))
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 4)
                                .background(p48.boolValue ? Color.orange : Color.secondary.opacity(0.15))
                                .foregroundStyle(p48.boolValue ? Color.black : Color.secondary)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                        }
                        .buttonStyle(.plain)
                    }

                    if let ph = strip.phase {
                        Button(action: {
                            state.setParameterBool(id: ph.id, value: !ph.boolValue)
                        }) {
                            Text("Ø")
                                .font(.system(size: 9, weight: .bold))
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 4)
                                .background(ph.boolValue ? Color.cyan : Color.secondary.opacity(0.15))
                                .foregroundStyle(ph.boolValue ? Color.black : Color.secondary)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }

            // 4. Aux Sends (Rotary Knobs)
            if !strip.auxSends.isEmpty {
                VStack(spacing: 6) {
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
                }
                .padding(.vertical, 2)
            }

            // 5. Pan Control (Rotary Knob)
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
                .padding(.vertical, 2)
            }

            // 6. Solo & Mute Buttons
            if strip.solo != nil || strip.mute != nil {
                HStack(spacing: 4) {
                    if let solo = strip.solo {
                        Button(action: {
                            state.setParameterBool(id: solo.id, value: !solo.boolValue)
                        }) {
                            Text("S")
                                .font(.system(size: 10, weight: .black))
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 4)
                                .background(solo.boolValue ? Color.yellow : Color.secondary.opacity(0.18))
                                .foregroundStyle(solo.boolValue ? Color.black : Color.secondary)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                        }
                        .buttonStyle(.plain)
                    }

                    if let mute = strip.mute {
                        Button(action: {
                            state.setParameterBool(id: mute.id, value: !mute.boolValue)
                        }) {
                            Text("M")
                                .font(.system(size: 10, weight: .black))
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 4)
                                .background(mute.boolValue ? Color.red : Color.secondary.opacity(0.18))
                                .foregroundStyle(mute.boolValue ? Color.white : Color.secondary)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }

            // 7. Main Fader & Peak Meter
            HStack(spacing: 4) {
                if let main = strip.mainSend {
                    VerticalAudioFader(
                        value: main.parameter.scalarValue,
                        range: main.parameter.scalarMin...main.parameter.scalarMax,
                        step: main.parameter.scalarStep,
                        unit: main.parameter.unit,
                        onValueChange: { state.setParameterScalar(id: main.parameter.id, value: $0) }
                    )
                }

                if let m = strip.meter {
                    VerticalAudioMeter(value: m.value, range: m.min...m.max, name: m.name)
                }
            }
            .frame(height: 180)

            // 8. Gain / dB Readout
            if let main = strip.mainSend {
                VStack(spacing: 1) {
                    Text(main.parameter.scalarValue <= main.parameter.scalarMin ? "-∞" : String(format: "%.1f dB", main.parameter.scalarValue))
                        .font(.system(size: 10, weight: .bold, design: .monospaced))
                        .foregroundStyle(main.parameter.scalarValue > 0 ? Color.red : Color.white)
                }
            }
        }
        .padding(8)
        .frame(width: 86)
        .background(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(Color(white: 0.11).opacity(0.95))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
        )
    }

    private var isPlayback: Bool {
        strip.name.contains("DAW") || strip.name.contains("Playback") || strip.name.contains("Return")
    }

    private func cleanModeName(_ n: String) -> String {
        return n.replacingOccurrences(of: " (Variable Gain)", with: "")
                .replacingOccurrences(of: " (Fixed Gain)", with: "")
    }
}
