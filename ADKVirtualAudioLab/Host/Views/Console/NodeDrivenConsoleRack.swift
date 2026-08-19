import SwiftUI

struct NodeDrivenConsoleRack: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var derivedChannels: [ChannelStripModel] {
        GenericAudioPresenter.deriveChannelStrips(from: snap)
    }

    private var derivedMasters: [OutputMasterStripModel] {
        GenericAudioPresenter.deriveOutputMasters(from: snap)
    }

    // Check if any mixer requested Matrix presentation explicitly
    private var matrixMixers: [MixerModel] {
        snap.mixers.filter { mixer in
            let hint = snap.presentation.mixerHint(for: mixer.id)
            return hint?.style == ASFW_MIXER_STYLE_MATRIX || (hint?.style == ASFW_MIXER_STYLE_AUTO && mixer.crosspoints.count > 32)
        }
    }

    var body: some View {
        StudioCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Console Strips") {
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 16) {
                    // 1. Channel Strips Bank (Hardware Inputs + DAW Returns)
                    if !derivedChannels.isEmpty {
                        VStack(alignment: .leading, spacing: 6) {
                            Text("CONSOLE CHANNELS")
                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                                .foregroundStyle(.cyan)

                            HStack(spacing: 8) {
                                ForEach(derivedChannels) { strip in
                                    VerticalChannelStrip(strip: strip, state: state)
                                }
                            }
                        }
                    }

                    // 2. Matrix Mixers (For large matrices like Saffire)
                    if !matrixMixers.isEmpty {
                        if !derivedChannels.isEmpty {
                            Divider().frame(height: 320)
                        }

                        ForEach(matrixMixers) { mixer in
                            MixerNodePresenter(mixer: mixer, snap: snap, state: state)
                        }
                    }

                    // 3. Master Outputs Bank (Main Mix, Aux Mix, HP A, HP B, SPDIF)
                    if !derivedMasters.isEmpty {
                        Divider().frame(height: 320)

                        VStack(alignment: .leading, spacing: 6) {
                            Text("OUTPUT MASTERS")
                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                                .foregroundStyle(.orange)

                            HStack(spacing: 8) {
                                ForEach(derivedMasters) { master in
                                    VerticalMasterStrip(master: master, state: state)
                                }
                            }
                        }
                    }
                }
                .padding(.vertical, 6)
            }
        }
    }
}
