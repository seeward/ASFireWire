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
                // Grouped Destination Rows (e.g. Phase88, FW1814)
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(bundleGroups) { grp in
                        HStack(spacing: 16) {
                            Text(grp.name)
                                .font(.system(size: 11, weight: .bold))
                                .foregroundStyle(.secondary)
                                .frame(width: 170, alignment: .leading)

                            if grp.bundleIds.count <= 3 {
                                // Compact Segmented Buttons for 2-3 options
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
                            } else {
                                // Sleek Dropdown Menu for 4+ routing choices (e.g. Phase88 7-source matrix)
                                let activeId = grp.bundleIds.first(where: { router.activeBundleIds.contains($0) })
                                let activeBundle = activeId.flatMap { bId in router.legalBundles.first(where: { $0.id == bId }) }
                                let currentLabel = activeBundle.map { shortBundleLabel($0) } ?? "Muted / None"

                                Menu {
                                    ForEach(grp.bundleIds, id: \.self) { bId in
                                        if let bundle = router.legalBundles.first(where: { $0.id == bId }) {
                                            let isSel = router.activeBundleIds.contains(bundle.id)
                                            Button(action: {
                                                state.setRouterBundleInGroup(
                                                    routerNodeId: router.id,
                                                    groupBundleIds: grp.bundleIds,
                                                    selectedBundleId: bundle.id
                                                )
                                            }) {
                                                if isSel {
                                                    Label(shortBundleLabel(bundle), systemImage: "checkmark")
                                                } else {
                                                    Text(shortBundleLabel(bundle))
                                                }
                                            }
                                        }
                                    }
                                } label: {
                                    HStack(spacing: 8) {
                                        Text(currentLabel)
                                            .font(.system(size: 11, weight: .semibold))
                                            .foregroundStyle(.primary)
                                        Spacer()
                                        Image(systemName: "chevron.up.chevron.down")
                                            .font(.system(size: 8))
                                            .foregroundStyle(.secondary)
                                    }
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 5)
                                    .frame(width: 250)
                                    .background(Color.secondary.opacity(0.12))
                                    .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                                    .overlay(
                                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                                            .strokeBorder(Color.white.opacity(0.1), lineWidth: 1)
                                    )
                                }
                                .menuStyle(.borderlessButton)
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

        // 1. Check Stream / Playback channel numbers
        if inN.contains("1") || inN.contains("Stream 1") || inN.contains("Playback 1") {
            if inN.contains("Stream") || inN.contains("Playback") { return "🎵 DAW Playback 1/2" }
        }
        if inN.contains("3") || inN.contains("Stream 3") || inN.contains("Playback 3") {
            if inN.contains("Stream") || inN.contains("Playback") { return "🎵 DAW Playback 3/4" }
        }
        if inN.contains("5") || inN.contains("Stream 5") || inN.contains("Playback 5") {
            if inN.contains("Stream") || inN.contains("Playback") { return "🎵 DAW Playback 5/6" }
        }
        if inN.contains("7") || inN.contains("Stream 7") || inN.contains("Playback 7") {
            if inN.contains("Stream") || inN.contains("Playback") { return "🎵 DAW Playback 7/8" }
        }
        if inN.contains("9") || inN.contains("Stream 9") || inN.contains("Playback 9") {
            if inN.contains("Stream") || inN.contains("Playback") { return "🎵 DAW Playback 9/10" }
        }

        // 2. Check Thru / Direct Inputs
        if inN.contains("Thru In 1") || inN.contains("Analog In 1") { return "🎸 Direct: Analog 1/2" }
        if inN.contains("Thru In 3") || inN.contains("Analog In 3") { return "🎸 Direct: Analog 3/4" }
        if inN.contains("Thru In 5") || inN.contains("Analog In 5") { return "🎸 Direct: Analog 5/6" }
        if inN.contains("Thru In 7") || inN.contains("Analog In 7") { return "🎸 Direct: Analog 7/8" }
        if inN.contains("Thru In 9") || inN.contains("SPDIF In") || inN.contains("Digital In") { return "🎛 Direct: S/PDIF In" }

        // 3. Check Mixer Sum
        if inN.contains("Mixer Out") || inN.contains("Mix ") { return "🎚 Digital Master Mix L/R" }

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
