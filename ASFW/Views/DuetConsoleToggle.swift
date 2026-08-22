import SwiftUI

struct DuetConsoleToggle: View {
    let title: String
    let accessibilityLabel: String
    let isOn: Bool
    let isWriting: Bool
    let action: () -> Void

    var body: some View {
        AudioConsoleStripToggle(
            title: title,
            accessibilityLabel: accessibilityLabel,
            isOn: isOn,
            isEnabled: !isWriting,
            tint: .accentColor,
            action: action
        )
    }
}
