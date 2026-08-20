import SwiftUI

struct VerticalAudioMeter: View {
    let values: [Double]
    let range: ClosedRange<Double>
    let name: String?

    init(value: Double, range: ClosedRange<Double>, name: String?) {
        self.init(values: [value], range: range, name: name)
    }

    /// Up to two bars share one meter slot, so a stereo bus does not make its
    /// strip wider than a mono one.
    init(values: [Double], range: ClosedRange<Double>, name: String?) {
        self.values = values.isEmpty ? [range.lowerBound] : Array(values.prefix(2))
        self.range = range
        self.name = name
    }

    private static let scale = LinearGradient(
        stops: [
            .init(color: .red, location: 0.0),
            .init(color: .orange, location: 0.18),
            .init(color: .yellow, location: 0.4),
            .init(color: .green, location: 0.75),
            .init(color: Color(red: 0.0, green: 0.85, blue: 0.25), location: 1.0)
        ],
        startPoint: .top,
        endPoint: .bottom
    )

    private var peak: Double { values.max() ?? range.lowerBound }

    private var isClipping: Bool { peak >= -0.5 }

    private func normalized(_ value: Double) -> Double {
        let span = range.upperBound - range.lowerBound
        guard span > 0 else { return 0.0 }
        return max(0.0, min(1.0, (value - range.lowerBound) / span))
    }

    var body: some View {
        VStack(spacing: ConsoleMetrics.s1) {
            Circle()
                .fill(isClipping ? Color.red : Color.red.opacity(0.18))
                .frame(width: 6, height: 6)
                .shadow(color: isClipping ? Color.red : Color.clear, radius: 4)

            GeometryReader { geo in
                let h = geo.size.height
                HStack(spacing: values.count > 1 ? 1 : 0) {
                    ForEach(Array(values.enumerated()), id: \.offset) { _, value in
                        ZStack(alignment: .bottom) {
                            RoundedRectangle(cornerRadius: 2)
                                .fill(Color(white: 0.06))
                                .overlay(
                                    RoundedRectangle(cornerRadius: 2)
                                        .strokeBorder(Color.white.opacity(0.15), lineWidth: 0.5)
                                )

                            RoundedRectangle(cornerRadius: 2)
                                .fill(Self.scale)
                                .frame(height: max(3, h * CGFloat(normalized(value))))
                        }
                    }
                }
            }
            .frame(width: ConsoleMetrics.meterBarsWidth)

            Text(formatDb(peak))
                .font(.system(size: 8, weight: .bold, design: .monospaced))
                .foregroundStyle(peak >= -3.0 ? .red : (peak >= -18.0 ? .yellow : .secondary))
                .lineLimit(1)
        }
        .frame(width: ConsoleMetrics.meterWidth)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(name ?? "Level meter")
        .accessibilityValue(peak <= -90.0 ? "Silent" : String(format: "%.0f decibels", peak))
    }

    private func formatDb(_ value: Double) -> String {
        if value <= -90.0 { return "-∞" }
        return String(format: "%.0f", value)
    }
}
