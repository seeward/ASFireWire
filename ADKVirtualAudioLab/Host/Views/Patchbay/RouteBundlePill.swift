import SwiftUI

struct RouteBundlePill: View {
    let routerId: UInt32
    let bundle: RouteBundleModel
    let isActive: Bool
    let label: String
    @ObservedObject var state: VirtualLabState

    var body: some View {
        Button(action: {
            state.toggleRouterBundle(routerNodeId: routerId, bundleId: bundle.id)
        }) {
            HStack(spacing: 6) {
                if isActive {
                    Image(systemName: "checkmark.circle.fill")
                        .font(.caption)
                        .foregroundStyle(.white)
                }
                Text(label)
                    .font(.caption.weight(isActive ? .bold : .medium))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(isActive ? Color.accentColor : Color.secondary.opacity(0.14))
            .foregroundStyle(isActive ? Color.white : Color.primary)
            .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .strokeBorder(isActive ? Color.accentColor : Color.white.opacity(0.08), lineWidth: 1)
            )
            .shadow(color: isActive ? Color.accentColor.opacity(0.4) : Color.clear, radius: 3)
        }
        .buttonStyle(.plain)
    }
}
