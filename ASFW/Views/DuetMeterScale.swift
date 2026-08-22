import SwiftUI

/// A single readable ruler shared by a mono or stereo Duet meter group.
struct DuetMeterScale: View {
    private static let marks = [0, -12, -24, -36, -48, -60]

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topTrailing) {
                ForEach(Self.marks, id: \.self) { value in
                    Text(value == 0 ? "0" : "\(value)")
                        .font(.system(size: 9, weight: .semibold).monospacedDigit())
                        .foregroundStyle(.secondary)
                        .offset(y: meterPosition(for: value, height: geometry.size.height))
                }
            }
        }
        .frame(width: 28, height: 168)
        .accessibilityHidden(true)
    }

    private func meterPosition(for decibels: Int, height: CGFloat) -> CGFloat {
        CGFloat(-decibels) / 60 * max(0, height - 12)
    }
}
