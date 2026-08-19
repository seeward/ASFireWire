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

// MARK: - Node-Driven Console Rack View

struct NodeDrivenConsoleRack: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var inputChannelGroups: [PresentationGroupModel] {
        snap.presentation.groups(ofKind: ASFW_PRES_GROUP_INPUT_CHANNEL)
    }

    private var monitorGroups: [PresentationGroupModel] {
        let mons = snap.presentation.groups(ofKind: ASFW_PRES_GROUP_MONITOR)
        return mons.isEmpty ? snap.presentation.groups(ofKind: ASFW_PRES_GROUP_OUTPUT_CHANNEL) : mons
    }

    var body: some View {
        StudioCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Live Rack") {
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 18) {
                    // A. Physical Inputs (from PresentationGroup if available)
                    if !inputChannelGroups.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("PHYSICAL INPUTS & PREAMPS")
                                .font(.caption.bold())
                                .foregroundStyle(.cyan)

                            HStack(spacing: 12) {
                                ForEach(inputChannelGroups) { grp in
                                    let params = grp.parameterIds.compactMap { snap.parameter(forId: $0) }
                                    let meter = grp.meterIds.compactMap { snap.meter(forId: $0) }.first

                                    VerticalChannelStrip(
                                        title: grp.name,
                                        params: params,
                                        meter: meter,
                                        state: state
                                    )
                                }
                            }
                        }
                    } else if !fallbackInputGroups.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("PHYSICAL INPUTS & PREAMPS")
                                .font(.caption.bold())
                                .foregroundStyle(.cyan)

                            HStack(spacing: 12) {
                                ForEach(fallbackInputGroups) { g in
                                    VerticalChannelStrip(
                                        title: g.id,
                                        params: g.params,
                                        meter: findInputMeter(for: g.id),
                                        state: state
                                    )
                                }
                            }
                        }
                    }

                    if (!snap.mixers.isEmpty || !masterParams.isEmpty) {
                        Divider().frame(height: 340)
                    }

                    // B. Mixer Nodes (Adaptive Geometry & Mixer Hints)
                    ForEach(snap.mixers) { mixer in
                        MixerNodePresenter(mixer: mixer, snap: snap, state: state)

                        if mixer.id != snap.mixers.last?.id || !masterParams.isEmpty {
                            Divider().frame(height: 340)
                        }
                    }

                    // C. Master Output Stage (from PresentationGroup if available)
                    if !masterParams.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("OUTPUT & MONITOR")
                                .font(.caption.bold())
                                .foregroundStyle(.orange)

                            VerticalMasterStrip(parameters: masterParams, meters: masterMeters, state: state)
                        }
                    }
                }
                .padding(.vertical, 8)
            }
        }
    }

    private var masterParams: [ParameterModel] {
        if !monitorGroups.isEmpty {
            let pIds = monitorGroups.flatMap { $0.parameterIds }
            let found = pIds.compactMap { snap.parameter(forId: $0) }
            if !found.isEmpty { return found }
        }
        return snap.parameters.filter {
            let n = $0.name.lowercased()
            return (n.contains("master") || n.contains("monitor") || n.contains("headphone") || n.contains("hp ") || n.contains("dim") || n.contains("main") || n.contains("analog output volume")) &&
                   !n.contains("mixer")
        }
    }

    private var masterMeters: [MeterModel] {
        if !monitorGroups.isEmpty {
            let mIds = monitorGroups.flatMap { $0.meterIds }
            let found = mIds.compactMap { snap.meter(forId: $0) }
            if !found.isEmpty { return found }
        }
        return snap.meters.filter {
            let n = $0.name.lowercased()
            return n.contains("out") || n.contains("master") || n.contains("mixer out") || n.contains("headphone")
        }
    }

    private struct GroupEntry: Identifiable {
        let id: String
        let params: [ParameterModel]
    }

    private var fallbackInputGroups: [GroupEntry] {
        var dict: [String: [ParameterModel]] = [:]
        for p in snap.parameters where isPreampParam(p) {
            let n = p.name.lowercased()
            if n.contains("input 1") || n.contains("ch 1") || n.contains("mic/inst 1") {
                dict["Input 1", default: []].append(p)
            } else if n.contains("input 2") || n.contains("ch 2") || n.contains("mic/inst 2") {
                dict["Input 2", default: []].append(p)
            } else if n.contains("input 3") || n.contains("ch 3") {
                dict["Input 3", default: []].append(p)
            } else if n.contains("input 4") || n.contains("ch 4") {
                dict["Input 4", default: []].append(p)
            } else {
                dict["Input Stage", default: []].append(p)
            }
        }
        return dict.keys.sorted().map { GroupEntry(id: $0, params: dict[$0]!) }
    }

    private func isPreampParam(_ p: ParameterModel) -> Bool {
        let n = p.name.lowercased()
        return (n.contains("input") || n.contains("preamp") || n.contains("+48v") ||
               n.contains("phase") || n.contains("xlr") || n.contains("inst") ||
               p.semantic == ASFW_SEMANTIC_PHANTOM_POWER || p.semantic == ASFW_SEMANTIC_PHASE_INVERT) &&
               !n.contains("mixer") && !n.contains("master")
    }

    private func findInputMeter(for channelName: String) -> MeterModel? {
        let num = channelName.filter { $0.isNumber }
        return snap.meters.first { m in
            let mn = m.name.lowercased()
            return (mn.contains("preamp") || mn.contains("linein")) && (num.isEmpty || mn.contains(num))
        }
    }
}
