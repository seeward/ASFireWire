import SwiftUI

struct RotaryKnob: View {
    let title: String
    let value: Double
    let range: ClosedRange<Double>
    let step: Double
    let unit: String
    let isBipolar: Bool
    let accentColor: Color
    let onValueChanged: (Double) -> Void

    @State private var dragStartY: CGFloat = 0
    @State private var dragStartValue: Double = 0

    init(
        title: String,
        value: Double,
        range: ClosedRange<Double> = -128...0,
        step: Double = 0.5,
        unit: String = "dB",
        isBipolar: Bool = false,
        accentColor: Color = .blue,
        onValueChanged: @escaping (Double) -> Void
    ) {
        self.title = title
        self.value = value
        self.range = range
        self.step = step
        self.unit = unit
        self.isBipolar = isBipolar
        self.accentColor = accentColor
        self.onValueChanged = onValueChanged
    }

    private var normalizedValue: Double {
        let span = range.upperBound - range.lowerBound
        guard span > 0 else { return 0 }
        return (value - range.lowerBound) / span
    }

    private var angle: Angle {
        // Rotary angle from -135 deg to +135 deg (270 degree sweep)
        let sweep = 270.0
        let current = (normalizedValue * sweep) - 135.0
        return .degrees(current)
    }

    private var formattedValue: String {
        if isBipolar {
            if abs(value) < 0.1 {
                return "C"
            } else if value < 0 {
                return "L\(Int(abs(value)))"
            } else {
                return "R\(Int(value))"
            }
        }
        if value <= range.lowerBound {
            return "-∞"
        }
        return String(format: "%.1f %@", value, unit)
    }

    var body: some View {
        VStack(spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 8, weight: .bold, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(1)

            ZStack {
                // Background Track Arc
                Circle()
                    .trim(from: 0.125, to: 0.875)
                    .stroke(
                        Color.white.opacity(0.12),
                        style: StrokeStyle(lineWidth: 3, lineCap: .round)
                    )
                    .rotationEffect(.degrees(90))
                    .frame(width: 32, height: 32)

                // Active Value Arc
                if isBipolar {
                    // Center to current position
                    let mid = 0.5
                    let start = min(mid, normalizedValue) * 0.75 + 0.125
                    let end = max(mid, normalizedValue) * 0.75 + 0.125
                    Circle()
                        .trim(from: start, to: end)
                        .stroke(
                            accentColor,
                            style: StrokeStyle(lineWidth: 3, lineCap: .round)
                        )
                        .rotationEffect(.degrees(90))
                        .frame(width: 32, height: 32)
                } else {
                    Circle()
                        .trim(from: 0.125, to: (normalizedValue * 0.75) + 0.125)
                        .stroke(
                            accentColor,
                            style: StrokeStyle(lineWidth: 3, lineCap: .round)
                        )
                        .rotationEffect(.degrees(90))
                        .frame(width: 32, height: 32)
                }

                // Knob Body
                Circle()
                    .fill(
                        LinearGradient(
                            colors: [Color(white: 0.28), Color(white: 0.14)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
                    .overlay(
                        Circle().stroke(Color.white.opacity(0.15), lineWidth: 1)
                    )
                    .shadow(color: .black.opacity(0.5), radius: 2, y: 1)
                    .frame(width: 24, height: 24)

                // Pointer Line
                Rectangle()
                    .fill(Color.white)
                    .frame(width: 1.5, height: 7)
                    .offset(y: -6)
                    .rotationEffect(angle)
            }
            .frame(width: 34, height: 34)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let deltaY = -gesture.translation.height
                        let deltaX = gesture.translation.width
                        let delta = Double(deltaY + deltaX * 0.5)
                        let span = range.upperBound - range.lowerBound
                        let stepFraction = span / 150.0 // 150 points for full sweep
                        let rawNewVal = value + (delta * (stepFraction / 8.0))
                        let clamped = min(max(rawNewVal, range.lowerBound), range.upperBound)
                        let rounded = (clamped / step).rounded() * step
                        if rounded != value {
                            onValueChanged(rounded)
                        }
                    }
            )
            .onTapGesture(count: 2) {
                // Double click resets to center / default
                let defaultVal = isBipolar ? 0.0 : (range.lowerBound > -100 ? 0.0 : range.lowerBound)
                onValueChanged(defaultVal)
            }

            Text(formattedValue)
                .font(.system(size: 8, weight: .semibold, design: .monospaced))
                .foregroundColor(.white.opacity(0.85))
                .lineLimit(1)
        }
        .frame(width: 52)
    }
}
