import SwiftUI

struct DuetInputStrip: View {
    let input: DuetConsoleSnapshot.InputStrip
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        VStack(spacing: 10) {
            Text(input.name.uppercased())
                .font(.caption.weight(.bold))
                .foregroundStyle(.tint)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .background(Color.accentColor.opacity(0.16), in: RoundedRectangle(cornerRadius: 6, style: .continuous))

            DuetNominalLevelMenu(
                inputName: input.name,
                control: input.nominalLevel,
                isWriting: isWriting(input.nominalLevel.id),
                submit: { submit(input.nominalLevel, $0) }
            )

            HStack(spacing: 6) {
                DuetConsoleToggle(
                    title: "+48",
                    accessibilityLabel: "\(input.name) phantom power",
                    isOn: input.phantomPower.value != 0,
                    isWriting: isWriting(input.phantomPower.id),
                    action: { submit(input.phantomPower, toggledValue(for: input.phantomPower)) }
                )
                DuetConsoleToggle(
                    title: "Ø",
                    accessibilityLabel: "\(input.name) phase invert",
                    isOn: input.phaseInvert.value != 0,
                    isWriting: isWriting(input.phaseInvert.id),
                    action: { submit(input.phaseInvert, toggledValue(for: input.phaseInvert)) }
                )
            }

            DuetVerticalFader(
                title: "\(input.name) gain",
                control: input.gain,
                isWriting: isWriting(input.gain.id),
                submit: { submit(input.gain, $0) }
            )

            DuetControlReadout(control: input.gain)
        }
        .frame(width: 112)
        .padding(10)
        .background(.tertiary.opacity(0.55), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private func toggledValue(for control: DuetConsoleSnapshot.Control) -> Int32 {
        control.value == 0 ? control.parameter.maximum : control.parameter.minimum
    }
}
