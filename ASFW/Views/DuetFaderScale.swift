import SwiftUI

/// A readout scale for one Duet fader. The labels come from the semantic
/// parameter range, rather than invented marks painted onto the track.
struct DuetFaderScale: View {
    let control: DuetConsoleSnapshot.Control

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topTrailing) {
                ForEach(marks) { mark in
                    Text(mark.label)
                        .font(.caption2.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .offset(y: (1 - mark.position) * max(0, geometry.size.height - 12))
                }
            }
        }
        .frame(width: 28, height: 168)
        .accessibilityHidden(true)
    }

    private var marks: [DuetFaderScaleMark] {
        switch control.parameter.unit {
        case .decibels:
            let minimum = Double(control.parameter.minimum)
            let maximum = Double(control.parameter.maximum)
            let span = maximum - minimum
            return (0...4).map { index in
                let position = 1 - Double(index) / 4
                let value = minimum + span * position
                return DuetFaderScaleMark(label: "\(Int(value.rounded()))", position: position)
            }
        case .normalized:
            return [
                DuetFaderScaleMark(label: "FULL", position: 1),
                DuetFaderScaleMark(label: "OFF", position: 0),
            ]
        case .none:
            return []
        }
    }
}
