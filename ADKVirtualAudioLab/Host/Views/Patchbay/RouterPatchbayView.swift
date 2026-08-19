import SwiftUI

// MARK: - Router Presenter (Adaptive Geometry & Presentation Hints)

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

struct RouterNodePresenter: View {
    let router: RouterModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var hint: RouterHintModel? {
        snap.presentation.routerHint(for: router.id)
    }

    private var style: ASFWRouterStyle {
        if let h = hint, h.style != ASFW_ROUTER_STYLE_AUTO {
            return h.style
        }
        return router.legalBundles.count > 16 ? ASFW_ROUTER_STYLE_MATRIX : ASFW_ROUTER_STYLE_SELECTOR
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(cleanRouterName(router.name))
                    .font(.subheadline.bold())
                Spacer()
                Text("\(router.activeBundleIds.count) active • (\(router.legalBundles.count) legal)")
                    .font(.caption2.monospaced())
                    .foregroundStyle(.secondary)
            }

            if style == ASFW_ROUTER_STYLE_MATRIX {
                // Crossbar Matrix View (e.g. Saffire 46x46)
                CrossbarMatrixGridView(router: router, hint: hint, snap: snap, state: state)
            } else if let bundleGroups = hint?.bundleGroups, !bundleGroups.isEmpty {
                // Grouped Selector Destination Rows (e.g. Phase88, FW1814)
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(bundleGroups) { grp in
                        HStack(spacing: 12) {
                            Text(grp.name)
                                .font(.system(size: 11, weight: .bold))
                                .foregroundStyle(.secondary)
                                .frame(width: 140, alignment: .leading)

                            HStack(spacing: 8) {
                                ForEach(grp.bundleIds, id: \.self) { bId in
                                    if let bundle = router.legalBundles.first(where: { $0.id == bId }) {
                                        RouteBundlePill(
                                            routerId: router.id,
                                            bundle: bundle,
                                            isActive: router.activeBundleIds.contains(bundle.id),
                                            label: shortBundleLabel(bundle),
                                            state: state
                                        )
                                    }
                                }
                            }
                        }
                        .padding(.vertical, 2)
                    }
                }
            } else {
                // Flat Selector Pills (e.g. Duet)
                FlowLayout(spacing: 8) {
                    ForEach(router.legalBundles) { bundle in
                        RouteBundlePill(
                            routerId: router.id,
                            bundle: bundle,
                            isActive: router.activeBundleIds.contains(bundle.id),
                            label: humanizedBundleLabel(bundle),
                            state: state
                        )
                    }
                }
            }
        }
        .padding(12)
        .background(Color.secondary.opacity(0.04))
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func cleanRouterName(_ n: String) -> String {
        return n.replacingOccurrences(of: "Input Source Router", with: "Input Source Selector (Preamps)")
                .replacingOccurrences(of: "Output Source Router", with: "Monitor Output Source")
    }

    private func shortBundleLabel(_ b: RouteBundleModel) -> String {
        if b.routes.isEmpty { return "Bundle #\(b.id)" }
        let first = b.routes[0]
        let inN = snap.portName(for: first.inputPortId)
        if inN.contains("Stream") || inN.contains("Playback") { return "Direct Playback" }
        if inN.contains("Mixer Out") || inN.contains("Mix ") { return "Mixer Out" }
        return cleanPortLabel(inN)
    }

    private func humanizedBundleLabel(_ b: RouteBundleModel) -> String {
        if b.routes.isEmpty { return "Bundle #\(b.id)" }

        if b.routes.count == 1 {
            let r = b.routes[0]
            let inN = snap.portName(for: r.inputPortId)
            if inN.contains("XLR 1") { return "🎤 Mic 1 (XLR)" }
            if inN.contains("Inst 1") { return "🎸 Instrument 1 (1/4\")" }
            if inN.contains("XLR 2") { return "🎤 Mic 2 (XLR)" }
            if inN.contains("Inst 2") { return "🎸 Instrument 2 (1/4\")" }
            return cleanPortLabel(inN)
        }

        let first = b.routes[0]
        let inN = snap.portName(for: first.inputPortId)
        if inN.contains("Playback") { return "🎵 DAW Playback 1/2" }
        if inN.contains("Mixer Out") { return "🎛 Direct Hardware Mixer L/R" }

        let outN = snap.portName(for: first.outputPortId)
        return "\(cleanPortLabel(inN)) ➔ \(cleanPortLabel(outN))"
    }

    private func cleanPortLabel(_ n: String) -> String {
        return n.replacingOccurrences(of: "Mux In: ", with: "")
                .replacingOccurrences(of: "Mux Out: ", with: "")
                .replacingOccurrences(of: "OutMux In: ", with: "")
                .replacingOccurrences(of: "OutMux Out: ", with: "")
                .replacingOccurrences(of: "Selected ", with: "Ch ")
                .replacingOccurrences(of: "Phys In: ", with: "")
                .replacingOccurrences(of: "Phys Out: ", with: "")
                .replacingOccurrences(of: "Host Playback: ", with: "DAW ")
                .replacingOccurrences(of: "Host Capture: ", with: "Cap ")
    }
}

// Large Crossbar Matrix Grid View (for 46x46 / large routers)
struct CrossbarMatrixGridView: View {
    let router: RouterModel
    let hint: RouterHintModel?
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    @State private var selectedOutputGroupIndex: Int = 0

    private var outputGroups: [PortGroupModel] {
        if let h = hint, !h.outputGroups.isEmpty {
            return h.outputGroups
        }
        return [PortGroupModel(name: "All Destinations", portIds: router.outputPortIds)]
    }

    private var currentDestinations: [UInt32] {
        guard selectedOutputGroupIndex < outputGroups.count else { return router.outputPortIds }
        return outputGroups[selectedOutputGroupIndex].portIds
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            // Category Filter Tabs
            if outputGroups.count > 1 {
                Picker("Destination Group", selection: $selectedOutputGroupIndex) {
                    ForEach(0..<outputGroups.count, id: \.self) { idx in
                        Text(outputGroups[idx].name).tag(idx)
                    }
                }
                .pickerStyle(.segmented)
                .frame(maxWidth: 650)
            }

            // Matrix Table
            ScrollView([.horizontal, .vertical], showsIndicators: true) {
                VStack(alignment: .leading, spacing: 2) {
                    // Header Row: Destination Output Ports
                    HStack(spacing: 2) {
                        Text("SOURCE \\ DEST")
                            .font(.system(size: 9, weight: .bold))
                            .foregroundStyle(.secondary)
                            .frame(width: 150, alignment: .leading)

                        ForEach(currentDestinations, id: \.self) { dstId in
                            Text(cleanDstName(snap.portName(for: dstId)))
                                .font(.system(size: 8, weight: .bold))
                                .foregroundStyle(.orange)
                                .lineLimit(1)
                                .frame(width: 65)
                        }
                    }
                    .padding(.vertical, 4)
                    .background(Color.secondary.opacity(0.1))

                    // Rows: Source Input Ports
                    ForEach(router.inputPortIds, id: \.self) { srcId in
                        HStack(spacing: 2) {
                            Text(cleanSrcName(snap.portName(for: srcId)))
                                .font(.system(size: 9, weight: .medium))
                                .lineLimit(1)
                                .frame(width: 150, alignment: .leading)

                            ForEach(currentDestinations, id: \.self) { dstId in
                                let bundle = router.legalBundles.first { b in
                                    b.routes.contains { $0.inputPortId == srcId && $0.outputPortId == dstId }
                                }
                                CrossbarRouteCell(
                                    routerId: router.id,
                                    bundle: bundle,
                                    isActive: bundle != nil && router.activeBundleIds.contains(bundle!.id),
                                    state: state
                                )
                                .frame(width: 65)
                            }
                        }
                        .padding(.vertical, 2)
                        .background(Color.secondary.opacity(0.02))
                    }
                }
            }
            .frame(maxHeight: 320)
        }
        .padding(10)
        .background(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(Color(white: 0.10).opacity(0.9))
        )
    }

    private func cleanSrcName(_ n: String) -> String {
        return n.replacingOccurrences(of: "Router In: ", with: "")
                .replacingOccurrences(of: "Phys In: ", with: "")
                .replacingOccurrences(of: "Host Playback: ", with: "DAW ")
    }

    private func cleanDstName(_ n: String) -> String {
        return n.replacingOccurrences(of: "Router Out: ", with: "")
                .replacingOccurrences(of: "Line/Monitor ", with: "Mon ")
                .replacingOccurrences(of: "DAW Record ", with: "Rec ")
                .replacingOccurrences(of: "Mixer In ", with: "MixIn ")
    }
}

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
