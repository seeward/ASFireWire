import SwiftUI

struct CrossbarRouteCell: View {
    let routerId: UInt32
    let bundle: RouteBundleModel?
    let isActive: Bool
    @ObservedObject var state: VirtualLabState

    var body: some View {
        if let b = bundle {
            Button(action: {
                state.toggleRouterBundle(routerNodeId: routerId, bundleId: b.id)
            }) {
                ZStack {
                    RoundedRectangle(cornerRadius: 3)
                        .fill(isActive ? Color.accentColor : Color.secondary.opacity(0.12))
                        .frame(width: 22, height: 18)

                    if isActive {
                        Image(systemName: "checkmark")
                            .font(.system(size: 8, weight: .bold))
                            .foregroundStyle(.white)
                    }
                }
            }
            .buttonStyle(.plain)
        } else {
            Rectangle()
                .fill(Color.clear)
                .frame(width: 22, height: 18)
        }
    }
}
