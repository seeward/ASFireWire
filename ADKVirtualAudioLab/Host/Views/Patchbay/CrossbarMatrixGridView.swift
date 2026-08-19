import SwiftUI

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
