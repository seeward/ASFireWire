import SwiftUI

struct DuetControlReadout: View {
    let control: DuetConsoleSnapshot.Control

    var body: some View {
        Text(valueText)
            .font(.caption.monospacedDigit().weight(.semibold))
            .foregroundStyle(control.value > control.parameter.minimum ? Color.primary : Color.secondary)
            .accessibilityHidden(true)
    }

    private var valueText: String {
        switch control.parameter.unit {
        case .decibels:
            return "\(control.value) dB"
        case .normalized:
            let span = control.parameter.maximum - control.parameter.minimum
            guard span > 0 else { return "0%" }
            let position = Double(control.value - control.parameter.minimum) / Double(span)
            return "\(Int((position * 100).rounded()))%"
        case .none:
            return "\(control.value)"
        }
    }
}
