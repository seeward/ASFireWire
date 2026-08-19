import SwiftUI

struct NodeDrivenRouterSection: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        StudioCard(title: "Signal Routing Patchbay", systemImage: "point.3.filled.connected.trianglepath.dotted", badge: "\(snap.routers.count) Routers") {
            VStack(alignment: .leading, spacing: 16) {
                ForEach(snap.routers) { router in
                    RouterNodePresenter(router: router, snap: snap, state: state)
                }
            }
        }
    }
}
