import SwiftUI

struct VerticalAudioFader: View {
    let value: Double
    let range: ClosedRange<Double>
    let step: Double
    let unit: String
    let onValueChange: (Double) -> Void

    @State private var isDragging = false

    private var normalizedValue: Double {
        let span = range.upperBound - range.lowerBound
        guard span > 0 else { return 0.0 }
        return max(0.0, min(1.0, (value - range.lowerBound) / span))
    }

    var body: some View {
        GeometryReader { geo in
            let trackHeight = max(10, geo.size.height - 28)
            let thumbY = (1.0 - normalizedValue) * trackHeight

            ZStack(alignment: .top) {
                // Scale Ticks along the sides
                FaderScaleTicks(height: trackHeight)
                    .frame(width: geo.size.width)
                    .offset(y: 14)

                // Central Fader Slot / Groove
                RoundedRectangle(cornerRadius: 3)
                    .fill(Color(white: 0.05))
                    .frame(width: 6, height: trackHeight)
                    .overlay(
                        RoundedRectangle(cornerRadius: 3)
                            .strokeBorder(Color.white.opacity(0.18), lineWidth: 1)
                    )
                    .offset(y: 14)

                // Fader Cap (Metallic studio style)
                ZStack {
                    RoundedRectangle(cornerRadius: 4, style: .continuous)
                        .fill(
                            LinearGradient(
                                colors: [Color(white: 0.42), Color(white: 0.22), Color(white: 0.32)],
                                startPoint: .top,
                                endPoint: .bottom
                            )
                        )
                        .frame(width: 36, height: 24)
                        .shadow(color: .black.opacity(0.6), radius: 3, y: 2)
                        .overlay(
                            RoundedRectangle(cornerRadius: 4, style: .continuous)
                                .strokeBorder(Color.white.opacity(0.3), lineWidth: 1)
                        )

                    // Center indicator line
                    Rectangle()
                        .fill(isDragging ? Color.cyan : Color.white)
                        .frame(width: 26, height: 2)
                        .shadow(color: isDragging ? Color.cyan.opacity(0.9) : Color.clear, radius: 3)
                }
                .offset(y: thumbY + 2)
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { gesture in
                            isDragging = true
                            let locationY = gesture.location.y - 14
                            let clampedY = max(0, min(trackHeight, locationY))
                            let newNorm = 1.0 - (clampedY / trackHeight)
                            let rawValue = range.lowerBound + newNorm * (range.upperBound - range.lowerBound)
                            let stepped = (rawValue / step).rounded() * step
                            let finalVal = max(range.lowerBound, min(range.upperBound, stepped))
                            onValueChange(finalVal)
                        }
                        .onEnded { _ in
                            isDragging = false
                        }
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(width: 48)
    }
}

struct FaderScaleTicks: View {
    let height: CGFloat

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                ForEach(0..<6) { i in
                    Rectangle()
                        .fill(Color.secondary.opacity(i == 1 || i == 5 ? 0.8 : 0.4))
                        .frame(width: i == 1 || i == 5 ? 7 : 4, height: 1)
                    if i < 5 { Spacer() }
                }
            }
            .frame(width: 7, height: height)

            Spacer()

            VStack(spacing: 0) {
                ForEach(0..<6) { i in
                    Rectangle()
                        .fill(Color.secondary.opacity(i == 1 || i == 5 ? 0.8 : 0.4))
                        .frame(width: i == 1 || i == 5 ? 7 : 4, height: 1)
                    if i < 5 { Spacer() }
                }
            }
            .frame(width: 7, height: height)
        }
    }
}
