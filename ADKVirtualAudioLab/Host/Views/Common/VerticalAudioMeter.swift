import SwiftUI

struct VerticalAudioMeter: View {
    let value: Double
    let range: ClosedRange<Double>
    let name: String?

    private var normalized: Double {
        let span = range.upperBound - range.lowerBound
        guard span > 0 else { return 0.0 }
        return max(0.0, min(1.0, (value - range.lowerBound) / span))
    }

    private var isClipping: Bool {
        return value >= -0.5
    }

    var body: some View {
        VStack(spacing: 4) {
            Circle()
                .fill(isClipping ? Color.red : Color.red.opacity(0.18))
                .frame(width: 6, height: 6)
                .shadow(color: isClipping ? Color.red : Color.clear, radius: 4)

            GeometryReader { geo in
                let h = geo.size.height
                ZStack(alignment: .bottom) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(Color(white: 0.06))
                        .overlay(
                            RoundedRectangle(cornerRadius: 2)
                                .strokeBorder(Color.white.opacity(0.15), lineWidth: 0.5)
                        )

                    RoundedRectangle(cornerRadius: 2)
                        .fill(
                            LinearGradient(
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
                        )
                        .frame(height: max(3, h * CGFloat(normalized)))
                }
            }
            .frame(width: 9)

            Text(formatDb(value))
                .font(.system(size: 8, weight: .bold, design: .monospaced))
                .foregroundStyle(value >= -3.0 ? .red : (value >= -18.0 ? .yellow : .secondary))
                .lineLimit(1)
        }
        .frame(width: 22)
    }

    private func formatDb(_ v: Double) -> String {
        if v <= -90.0 { return "-∞" }
        return String(format: "%.0f", v)
    }
}
