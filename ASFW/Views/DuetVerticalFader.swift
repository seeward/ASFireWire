import SwiftUI

struct DuetVerticalFader: View {
    let title: String
    let control: DuetConsoleSnapshot.Control
    let isWriting: Bool
    let submit: (Int32) -> Void

    var body: some View {
        AudioConsoleVerticalFader(
            title: title,
            value: Double(control.value),
            range: Double(control.parameter.minimum)...Double(control.parameter.maximum),
            step: Double(max(1, control.parameter.step)),
            tint: .white,
            trackWidth: 7,
            thumbSize: CGSize(width: 34, height: 22),
            fillsTrack: false,
            isEnabled: !isWriting,
            valueDescription: displayValue,
            onValueCommitted: commit
        )
        .frame(width: 46, height: 168)
    }

    private func displayValue(_ value: Double) -> String {
        control.parameter.unit == .decibels ? "\(Int(value.rounded())) dB" : "\(Int(value.rounded()))"
    }

    private func commit(_ value: Double) {
        submit(Int32(value.rounded()))
    }
}
