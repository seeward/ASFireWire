import SwiftUI

struct AudioTopologyOutputStrip: View {
    let master: AudioTopologyOutputMaster
    let meters: AudioMeterSnapshot?
    let setLevel: ([MAudio1814ControlID], Double) -> Void

    var body: some View {
        VStack(spacing: 8) {
            Text(master.name).font(.caption.monospaced().bold()).lineLimit(2).multilineTextAlignment(.center)
                .foregroundStyle(.orange).frame(maxWidth: .infinity, minHeight: 32)
                .background(Color.orange.opacity(0.18)).clipShape(RoundedRectangle(cornerRadius: 4))
            Spacer(minLength: 8)
            HStack(spacing: 4) {
                AudioTopologyFader(value: master.level, onChanged: { setLevel(master.levelControls, $0) })
                    .frame(height: 180)
                AudioTopologyMeter(pair: master.meterPair, snapshot: meters, name: master.name)
                    .frame(height: 180)
            }
            Text("\(Int(master.level.rounded()))%")
                .font(.caption.monospaced().bold()).foregroundStyle(.orange)
        }
        .padding(8)
        .frame(width: 100, height: 420)
        .background(AudioTopologyStripBackground(stroke: .orange))
    }
}
