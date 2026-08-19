import SwiftUI

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
