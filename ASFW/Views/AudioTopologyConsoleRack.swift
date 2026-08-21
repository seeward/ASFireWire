import SwiftUI

struct AudioTopologyConsoleRack: View {
    let topology: AudioTopologySnapshot
    let meters: AudioMeterSnapshot?
    let setLevel: (MAudio1814ControlID, Double) -> Void
    let setSend: (AudioTopologySend, Bool) -> Void

    var body: some View {
        AudioTopologyCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Console Strips") {
            ScrollView(.horizontal, showsIndicators: true) {
                HStack(alignment: .top, spacing: 16) {
                    AudioTopologyStripBank(caption: "CONSOLE CHANNELS", tint: .cyan) {
                        ForEach(topology.channels) { channel in
                            AudioTopologyChannelStrip(channel: channel, meters: meters, setSend: setSend)
                        }
                    }
                    Rectangle().fill(Color.white.opacity(0.08)).frame(width: 1).frame(maxHeight: .infinity)
                    AudioTopologyStripBank(caption: "OUTPUT MASTERS", tint: .orange) {
                        ForEach(topology.outputMasters) { master in
                            AudioTopologyOutputStrip(master: master, meters: meters, setLevel: setLevel)
                        }
                    }
                }
                .padding(.vertical, 4)
            }
        }
    }
}

private struct AudioTopologyStripBank<Content: View>: View {
    let caption: String
    let tint: Color
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(caption).font(.caption.monospaced().bold()).foregroundStyle(tint)
            HStack(alignment: .top, spacing: 8, content: content)
        }
    }
}
