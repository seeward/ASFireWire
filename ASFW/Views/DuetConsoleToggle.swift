import SwiftUI

struct DuetConsoleToggle: View {
    let title: String
    let accessibilityLabel: String
    let isOn: Bool
    let isWriting: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.caption.weight(.bold))
                .frame(maxWidth: .infinity, minHeight: 26)
                .foregroundStyle(isOn ? Color.white : Color.secondary)
                .background(isOn ? Color.accentColor : Color.secondary.opacity(0.18), in: RoundedRectangle(cornerRadius: 6, style: .continuous))
        }
        .buttonStyle(.plain)
        .disabled(isWriting)
        .accessibilityLabel(accessibilityLabel)
        .accessibilityValue(isOn ? "On" : "Off")
        .accessibilityAddTraits(isOn ? .isSelected : [])
    }
}
