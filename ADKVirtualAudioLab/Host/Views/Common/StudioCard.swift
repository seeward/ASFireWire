import SwiftUI

struct StudioCard<Content: View>: View {
    let title: String
    let systemImage: String
    var badge: String? = nil
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: ConsoleMetrics.s3) {
            HStack(spacing: ConsoleMetrics.s2) {
                Image(systemName: systemImage)
                    .foregroundStyle(.tint)
                    .font(.headline)
                Text(title)
                    .font(.headline)
                    .fontWeight(.bold)
                if let badge = badge {
                    Text(badge)
                        .font(.caption2.weight(.bold))
                        .padding(.horizontal, ConsoleMetrics.s2)
                        .padding(.vertical, 3)
                        .background(Color.secondary.opacity(0.18))
                        .clipShape(Capsule())
                }
                Spacer()
            }

            content()
        }
        .padding(ConsoleMetrics.s4)
        .background(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rCard, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.7))
        )
        .overlay(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rCard, style: .continuous)
                .strokeBorder(ConsoleMetrics.stripStroke, lineWidth: 1)
        )
    }
}
