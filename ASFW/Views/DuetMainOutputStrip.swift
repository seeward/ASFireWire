import SwiftUI

struct DuetMainOutputStrip: View {
    let output: DuetConsoleSnapshot.MainOutput
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        AudioConsoleStripShell(
            title: "MAIN OUT",
            tint: .orange,
            width: 176,
            borderTint: output.hardwareSelected ? .orange : nil
        ) {
            AudioConsoleStripSlots(controlHeight: 92, faderHeight: 220) {
                VStack(spacing: 7) {
                    Menu {
                        Button("DAW") { submit(output.source, 0) }
                        Button("Cue Mixer") { submit(output.source, 1) }
                    } label: {
                        Text(output.source.value == 0 ? "DAW" : "Cue Mixer")
                            .font(.caption.weight(.semibold)).frame(maxWidth: .infinity, minHeight: 28)
                    }
                    .menuStyle(.borderlessButton)
                    .disabled(isWriting(output.source.id))

                    Menu {
                        Button("Instrument") { submit(output.nominalLevel, 0) }
                        Button("−10 dBV") { submit(output.nominalLevel, 1) }
                    } label: {
                        Text(output.nominalLevel.value == 0 ? "Instrument" : "−10 dBV")
                            .font(.caption.weight(.semibold)).frame(maxWidth: .infinity, minHeight: 28)
                    }
                    .menuStyle(.borderlessButton)
                    .disabled(isWriting(output.nominalLevel.id))

                    DuetConsoleToggle(
                        title: "MUTE",
                        accessibilityLabel: "Main output mute",
                        isOn: output.mute.value != 0,
                        isWriting: isWriting(output.mute.id),
                        action: { submit(output.mute, toggledValue) }
                    )

                    HStack(spacing: 4) {
                        muteFollowMenu(title: "MAIN", control: output.mainMuteFollow)
                        muteFollowMenu(title: "HP", control: output.headphoneMuteFollow)
                    }
                }
            } faderBlock: {
                VStack(spacing: 5) {
                    HStack(alignment: .bottom, spacing: 6) {
                        DuetVerticalFader(title: "Main output level", control: output.level,
                                          isWriting: isWriting(output.level.id), submit: { submit(output.level, $0) })
                        DuetFaderScale(control: output.level)
                        DuetStereoMeters(levels: output.meterLevels, label: "Main output")
                        DuetMeterScale()
                    }
                    DuetControlReadout(control: output.level)
                }
            }
        }
    }

    private var toggledValue: Int32 {
        output.mute.value == 0 ? output.mute.parameter.maximum : output.mute.parameter.minimum
    }

    @ViewBuilder
    private func muteFollowMenu(title: String, control: DuetConsoleSnapshot.Control) -> some View {
        let outputName = title == "HP" ? "headphones" : "main output"
        Menu {
            Button("Off") { submit(control, 0) }
            Button("On mute") { submit(control, 1) }
            Button("On unmute") { submit(control, 2) }
        } label: {
            Text("\(title) · \(muteFollowLabel(control.value))")
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .lineLimit(1)
                .frame(maxWidth: .infinity, minHeight: 22)
        }
        .menuStyle(.borderlessButton)
        .disabled(isWriting(control.id))
        .accessibilityLabel("Mute button behavior for \(outputName)")
    }

    private func muteFollowLabel(_ value: Int32) -> String {
        switch value {
        case 1: "MUTE"
        case 2: "UNMUTE"
        default: "OFF"
        }
    }
}
