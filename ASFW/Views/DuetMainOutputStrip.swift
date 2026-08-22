import SwiftUI

struct DuetMainOutputStrip: View {
    let output: DuetConsoleSnapshot.MainOutput
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        VStack(spacing: 10) {
            Text("MAIN OUT")
                .font(.caption.weight(.bold))
                .foregroundStyle(.orange)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .background(Color.orange.opacity(0.16), in: RoundedRectangle(cornerRadius: 6, style: .continuous))

            DuetConsoleToggle(
                title: "MUTE",
                accessibilityLabel: "Main output mute",
                isOn: output.mute.value != 0,
                isWriting: isWriting(output.mute.id),
                action: { submit(output.mute, toggledValue) }
            )

            DuetVerticalFader(
                title: "Main output level",
                control: output.level,
                isWriting: isWriting(output.level.id),
                submit: { submit(output.level, $0) }
            )

            DuetControlReadout(control: output.level)
        }
        .frame(width: 112)
        .padding(10)
        .background(.tertiary.opacity(0.55), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var toggledValue: Int32 {
        output.mute.value == 0 ? output.mute.parameter.maximum : output.mute.parameter.minimum
    }
}
