import SwiftUI

struct DuetConsoleCard<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title)
                .font(.headline)
                .foregroundStyle(.secondary)

            content()
        }
        .padding()
        .background(.quaternary, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}
