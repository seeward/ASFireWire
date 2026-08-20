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

                    // 5. Presentation-Driven DSP Processors (only if device defines DSP processor groups)
                    let procGroups = snap.presentation.groups(ofKind: ASFW_PRES_GROUP_PROCESSOR)
                    if !procGroups.isEmpty {
                        let procNodeIds = Set(procGroups.flatMap { $0.nodeIds })
                        let dspNodes = snap.nodes.filter { procNodeIds.contains($0.id) }
                        if !dspNodes.isEmpty {
                            NodeDrivenProcessorsSection(procNodes: dspNodes, snap: snap, state: state)
                        }
                    }

                    // 6. What the controls did, straight from the model's log
                    BehaviourLogSection(events: state.events, state: state)
                } else {
                    ProgressView("Connecting to virtual audio runtime…")
                        .frame(maxWidth: .infinity, minHeight: 250)
                }
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .task {
            state.refresh()
        }
    }
}
