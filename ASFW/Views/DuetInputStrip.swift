import SwiftUI

struct DuetInputStrip: View {
    let input: DuetConsoleSnapshot.InputStrip
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        AudioConsoleStripShell(
            title: input.name.uppercased(),
            tint: .cyan,
            width: 142,
            borderTint: input.hardwareSelected ? .cyan : nil
        ) {
            AudioConsoleStripSlots(controlHeight: 92, faderHeight: 220) {
                VStack(spacing: 7) {
                    DuetSourceMenu(input: input, isWriting: isWriting, submit: submit)
                    if !input.isInstrument {
                        DuetNominalLevelMenu(inputName: input.name, control: input.nominalLevel,
                                              isWriting: isWriting(input.nominalLevel.id),
                                              submit: { submit(input.nominalLevel, $0) })
                    }

                    HStack(spacing: 6) {
                        DuetConsoleToggle(
                            title: "+48",
                            accessibilityLabel: "\(input.name) phantom power",
                            isOn: input.phantomPower.value != 0,
                            isWriting: isWriting(input.phantomPower.id),
                            action: { submit(input.phantomPower, toggledValue(for: input.phantomPower)) }
                        )
                        .disabled(input.isInstrument || input.isFixedLevel)
                        DuetConsoleToggle(
                            title: "Ø",
                            accessibilityLabel: "\(input.name) phase invert",
                            isOn: input.phaseInvert.value != 0,
                            isWriting: isWriting(input.phaseInvert.id),
                            action: { submit(input.phaseInvert, toggledValue(for: input.phaseInvert)) }
                        )
                    }
                }
            } faderBlock: {
                VStack(spacing: 5) {
                    HStack(alignment: .bottom, spacing: 6) {
                        if let gain = input.gain {
                            DuetVerticalFader(title: "\(input.name) gain", control: gain,
                                              isWriting: isWriting(gain.id), submit: { submit(gain, $0) })
                            DuetFaderScale(control: gain)
                        } else {
                            Text(input.nominalLevel.value == 1 ? "Fixed +4" : "Fixed −10")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.secondary)
                                .frame(width: 74, height: 168, alignment: .bottom)
                        }
                        DuetLevelMeter(value: input.meterLevel, label: "\(input.name) level")
                        DuetMeterScale()
                    }
                    if let gain = input.gain {
                        DuetControlReadout(control: gain)
                    }
                }
            }
        }
    }

    private func toggledValue(for control: DuetConsoleSnapshot.Control) -> Int32 {
        control.value == 0 ? control.parameter.maximum : control.parameter.minimum
    }
}

private struct DuetSourceMenu: View {
    let input: DuetConsoleSnapshot.InputStrip
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        Menu {
            Button("XLR") { submit(input.source, 0) }
            Button("Instrument") { submit(input.source, 1) }
        } label: {
            Label(input.isInstrument ? "Instrument" : "XLR", systemImage: "cable.connector")
                .font(.caption.weight(.semibold))
                .frame(maxWidth: .infinity, minHeight: 28)
        }
        .menuStyle(.borderlessButton)
        .disabled(isWriting(input.source.id))
        .accessibilityLabel("\(input.name) source")
    }
}
