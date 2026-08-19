import SwiftUI

// MARK: - Top-Level Node-Driven Audio Dashboard

struct GenericLabDashboardView: View {
    @StateObject private var state = VirtualLabState()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if let snap = state.snapshot {
                    // 1. Device Header Bar
                    DeviceHeaderBar(snap: snap, state: state)

                    // 2. Hardware Waists & Architecture Summary
                    DeviceWaistsSection(snap: snap, state: state)

                    // 3. Presentation-Driven Hardware Console Rack
                    NodeDrivenConsoleRack(snap: snap, state: state)

                    // 4. Presentation-Driven Signal Routing / Patchbay
                    NodeDrivenRouterSection(snap: snap, state: state)

                    // 5. Presentation-Driven DSP Processors
                    let procGroups = snap.presentation.groups(ofKind: ASFW_PRES_GROUP_PROCESSOR)
                    let procNodes = snap.nodes.filter { $0.kind == ASFW_NODE_PROCESSOR }
                    if !procGroups.isEmpty || !procNodes.isEmpty {
                        NodeDrivenProcessorsSection(procNodes: procNodes, snap: snap, state: state)
                    }
                } else {
                    ProgressView("Connecting to virtual audio runtime…")
                        .frame(maxWidth: .infinity, minHeight: 250)
                }
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }
}
