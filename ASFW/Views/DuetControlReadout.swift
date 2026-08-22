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
        case .decibels: "\(control.value) dB"
        case .normalized: "\(control.value)"
        case .none: "\(control.value)"
        }
    }
}
