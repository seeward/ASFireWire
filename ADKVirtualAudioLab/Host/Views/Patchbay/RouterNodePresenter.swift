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

    /// The model resolves a bundle's source by walking the topology back to a
    /// connector or a bus, so there is nothing here to infer from port names.
    private func shortBundleLabel(_ b: RouteBundleModel) -> String {
        if !b.sourceLabel.isEmpty { return b.sourceLabel }
        if b.routes.isEmpty { return "Bundle #\(b.id)" }
        return cleanPortLabel(snap.portName(for: b.routes[0].inputPortId))
    }

    private func humanizedBundleLabel(_ b: RouteBundleModel) -> String {
        if !b.sourceLabel.isEmpty { return b.sourceLabel }
        if b.routes.isEmpty { return "Bundle #\(b.id)" }

        let first = b.routes[0]
        let inName = cleanPortLabel(snap.portName(for: first.inputPortId))
        if b.routes.count == 1 { return inName }
        return "\(inName) ➔ \(cleanPortLabel(snap.portName(for: first.outputPortId)))"
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
