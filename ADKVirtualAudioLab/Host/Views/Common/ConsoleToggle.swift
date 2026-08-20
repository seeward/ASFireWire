import SwiftUI

/// A latching console button: S, M, Ø, +48V, MUTE, DIM.
///
/// Fills whatever height the row schema gives it, so every toggle in a bank is
/// the same size regardless of glyph length.
struct ConsoleToggle: View {
    let glyph: String
    let label: String
    let isOn: Bool
    let tint: Color
    var onForeground: Color = .black
    var fontSize: CGFloat = 10
    var glowsWhenOn: Bool = false
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(glyph)
                .font(.system(size: fontSize, weight: .black))
                .lineLimit(1)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(isOn ? tint : Color.secondary.opacity(0.18))
                .foregroundStyle(isOn ? onForeground : Color.secondary)
                .clipShape(RoundedRectangle(cornerRadius: ConsoleMetrics.rControl))
                .shadow(color: glowsWhenOn && isOn ? tint.opacity(0.8) : .clear, radius: 3)
        }
        .buttonStyle(.plain)
        .accessibilityLabel(label)
        .accessibilityAddTraits(isOn ? [.isSelected] : [])
    }
}
