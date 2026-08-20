import SwiftUI

struct NodeDrivenConsoleRack: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    // Check if any mixer requested Matrix presentation explicitly
    private var matrixMixers: [MixerModel] {
        snap.mixers.filter { GenericAudioPresenter.isMatrixMixer($0, in: snap) }
    }

    var body: some View {
        let channels = GenericAudioPresenter.deriveChannelStrips(from: snap)
        let masters = GenericAudioPresenter.deriveOutputMasters(from: snap)
        let plan = ConsoleRowPlan.plan(channels: channels, masters: masters)

        StudioCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Console Strips") {
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: ConsoleMetrics.s4) {
                    // 1. Channel Strips Bank (Hardware Inputs + DAW Returns)
                    if !channels.isEmpty {
                        bank(caption: "CONSOLE CHANNELS", tint: .cyan) {
                            ForEach(channels) { strip in
                                VerticalChannelStrip(strip: strip, plan: plan, state: state)
                            }
                        }
                    }

                    // 2. Matrix Mixers (For large matrices like Saffire)
                    if !matrixMixers.isEmpty {
                        if !channels.isEmpty { bankDivider }

                        ForEach(matrixMixers) { mixer in
                            MixerNodePresenter(mixer: mixer, snap: snap, state: state)
                        }
                    }

                    // 3. Master Outputs Bank (Main Mix, Aux Mix, HP A, HP B, SPDIF)
                    if !masters.isEmpty {
                        bankDivider

                        bank(caption: "OUTPUT MASTERS", tint: .orange) {
                            ForEach(masters) { master in
                                VerticalMasterStrip(master: master, plan: plan, state: state)
                            }
                        }
                    }
                }
                .padding(.vertical, ConsoleMetrics.s1)
            }
        }
    }

    @ViewBuilder
    private func bank<Content: View>(
        caption: String,
        tint: Color,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: ConsoleMetrics.s2) {
            Text(caption)
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(tint)

            HStack(alignment: .top, spacing: ConsoleMetrics.s2) {
                content()
            }
        }
    }

    /// Spans the full bank height rather than a fixed guess, so it always
    /// matches the strips beside it.
    private var bankDivider: some View {
        Rectangle()
            .fill(ConsoleMetrics.stripStroke)
            .frame(width: 1)
            .frame(maxHeight: .infinity)
    }
}
