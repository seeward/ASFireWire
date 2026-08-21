import SwiftUI

/// The console. One horizontally scrolling rack — inputs, software returns and
/// outputs in signal order — rather than the vendor's mixer/output tab split,
/// which forced you to leave the page to find the fader you wanted.
struct AudioTopologyConsoleRack: View {
    let topology: AudioTopologySnapshot
    let meters: AudioMeterSnapshot?
    let peakHold: AudioMeterPeakHold
    let viewModel: MAudio1814ConfigurationViewModel

    private var inputStrips: [AudioTopologyStrip] {
        topology.strips.filter { $0.kind.isInput }
    }

    private var outputStrips: [AudioTopologyStrip] {
        topology.strips.filter { !$0.kind.isInput }
    }

    var body: some View {
        AudioTopologyCard(title: "Hardware Audio Console",
                          systemImage: "slider.vertical.3",
                          badge: viewModel.isSoloActive ? "Solo active" : "Console") {
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 14) {
                    bank("INPUTS", tint: .cyan, strips: inputStrips)
                    Rectangle().fill(Color.white.opacity(0.08))
                        .frame(width: 1).frame(maxHeight: .infinity)
                    bank("OUTPUTS", tint: .orange, strips: outputStrips)
                }
                .padding(.vertical, 4)
                .fixedSize(horizontal: false, vertical: true)
            }
            AudioTopologySignalFlowLegend()
        }
    }

    private func bank(_ caption: String, tint: Color,
                      strips: [AudioTopologyStrip]) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(caption).font(.system(size: 10).monospaced().bold()).foregroundStyle(tint)
            HStack(alignment: .top, spacing: 6) {
                ForEach(strips) { strip in
                    AudioTopologyChannelStrip(
                        strip: strip,
                        meters: meters,
                        peakHold: peakHold,
                        isLinked: viewModel.isLinked(strip),
                        isMuted: viewModel.isMuted(strip),
                        isSoloed: viewModel.isSoloed(strip),
                        isControlled: viewModel.isControlled(strip),
                        isSuppressed: viewModel.isSuppressed(strip),
                        level: { viewModel.displayedLevel(strip, $0) },
                        setLevel: { viewModel.setLevel(strip, $0, position: $1) },
                        setPan: { viewModel.setPan(strip, $0, position: $1) },
                        setAux: { viewModel.setAux($0, position: $1) },
                        setSend: viewModel.setTopologySend,
                        setSource: viewModel.applyMixerControl,
                        toggleLink: { viewModel.toggleLink(strip) },
                        toggleMute: { viewModel.toggleMute(strip) },
                        toggleSolo: { viewModel.toggleSolo(strip) },
                        toggleControl: { viewModel.toggleControl(strip) })
                }
            }
        }
    }
}

/// Where the two buses actually go.
///
/// A send knob with no visible destination is unreadable — "aux" on an input
/// strip means nothing until you know the aux bus is what the headphones can be
/// switched to. The device has exactly two mixes and four destinations, so the
/// whole routing fits in one line rather than needing a patchbay.
private struct AudioTopologySignalFlowLegend: View {
    var body: some View {
        HStack(alignment: .top, spacing: 22) {
            flow(tint: .blue, title: "out 1/2 · 3/4",
                 detail: "fader → the two stereo mixes → 1/2 OUT, 3/4 OUT, phones (mon 1/2 / 3/4)")
            flow(tint: .yellow, title: "aux",
                 detail: "aux knob → the AUX bus → AUX OUT, or any output set to Aux")
            flow(tint: .teal, title: "ctrl",
                 detail: "the front-panel assignable knob drives every strip marked ctrl")
            Spacer(minLength: 0)
        }
        .padding(.top, 2)
    }

    private func flow(tint: Color, title: String, detail: String) -> some View {
        HStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 2).fill(tint).frame(width: 3, height: 22)
            VStack(alignment: .leading, spacing: 1) {
                Text(title).font(.system(size: 10, weight: .bold).monospaced())
                    .foregroundStyle(tint)
                Text(detail).font(.system(size: 10)).foregroundStyle(.secondary)
            }
        }
    }
}
