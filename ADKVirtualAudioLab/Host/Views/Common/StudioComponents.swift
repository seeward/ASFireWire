import SwiftUI

// MARK: - Pro Audio Studio Fader

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

// MARK: - Pro Audio Segmented LED Peak Meter

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

// Studio Design Card Container
struct StudioCard<Content: View>: View {
    let title: String
    let systemImage: String
    var badge: String? = nil
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 8) {
                Image(systemName: systemImage)
                    .foregroundStyle(.tint)
                    .font(.headline)
                Text(title)
                    .font(.headline)
                    .fontWeight(.bold)
                if let badge = badge {
                    Text(badge)
                        .font(.caption2.weight(.bold))
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(Color.secondary.opacity(0.18))
                        .clipShape(Capsule())
                }
                Spacer()
            }

            content()
        }
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.7))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
        )
    }
}

// FlowLayout for clean wrapping
struct FlowLayout: Layout {
    var spacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 0
        var height: CGFloat = 0
        var x: CGFloat = 0
        var y: CGFloat = 0
        var maxHeightInRow: CGFloat = 0

        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > width && x > 0 {
                x = 0
                y += maxHeightInRow + spacing
                maxHeightInRow = 0
            }
            maxHeightInRow = max(maxHeightInRow, size.height)
            x += size.width + spacing
        }
        height = y + maxHeightInRow
        return CGSize(width: width, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX
        var y = bounds.minY
        var maxHeightInRow: CGFloat = 0

        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > bounds.maxX && x > bounds.minX {
                x = bounds.minX
                y += maxHeightInRow + spacing
                maxHeightInRow = 0
            }
            subview.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
            maxHeightInRow = max(maxHeightInRow, size.height)
            x += size.width + spacing
        }
    }
}
