import SwiftUI

// MARK: - Mixer Node Presenter (Adaptive Geometry & Presentation Hints)

struct MixerNodePresenter: View {
    let mixer: MixerModel
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var hint: MixerHintModel? {
        snap.presentation.mixerHint(for: mixer.id)
    }

    private var isSmallMixer: Bool {
        if let h = hint, h.style != ASFW_MIXER_STYLE_AUTO {
            return h.style == ASFW_MIXER_STYLE_CHANNEL_STRIPS
        }
        return mixer.crosspoints.count <= 8 && mixer.outputPortIds.count <= 2
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text(mixer.name.uppercased())
                    .font(.caption.bold())
                    .foregroundStyle(.tint)

                Text("(\(mixer.inputPortIds.count)×\(mixer.outputPortIds.count) • \(mixer.crosspoints.count) Crosspoints)")
                    .font(.caption2.monospaced())
                    .foregroundStyle(.secondary)
            }

            if isSmallMixer {
                // Small Mixer: Source Channel Cards with Send Faders
                HStack(spacing: 12) {
                    ForEach(groupedBySource(mixer)) { grp in
                        MixerSourceChannelCard(group: grp, mixer: mixer, snap: snap, state: state)
                    }
                }
            } else {
                // Large / Matrix Mixer: Elegant Matrix Grid View
                MixerMatrixTableView(mixer: mixer, snap: snap, state: state)
            }
        }
    }

    struct SourceGroup: Identifiable {
        let id: String
        let inputPortId: UInt32
        let crosspoints: [MixerCrosspointModel]
    }

    private func groupedBySource(_ mx: MixerModel) -> [SourceGroup] {
        var dict: [UInt32: [MixerCrosspointModel]] = [:]
        for cp in mx.crosspoints {
            dict[cp.inputPortId, default: []].append(cp)
        }
        return dict.keys.sorted().map { inId in
            SourceGroup(
                id: snap.portName(for: inId).replacingOccurrences(of: "Phys In: ", with: "").replacingOccurrences(of: "Host Playback: ", with: "DAW "),
                inputPortId: inId,
                crosspoints: dict[inId]!
            )
        }
    }
}
