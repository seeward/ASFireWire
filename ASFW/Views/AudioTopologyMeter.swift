import SwiftUI

/// A single channel's peak meter, with a held-peak marker. Scale and ballistics
/// come from `MAudio1814Level` / `AudioMeterPeakHold`, never from the view.
struct AudioTopologyMeter: View {
    let index: Int?
    let snapshot: AudioMeterSnapshot?
    let peakHold: AudioMeterPeakHold
    let name: String

    /// Drawn straight from the device. Its value already holds across polls, so
    /// there is nothing here to smooth — see `AudioMeterPeakHold`.
    private var barRaw: Int16 {
        guard let index, let snapshot, snapshot.values.indices.contains(index) else { return 0 }
        return snapshot.values[index]
    }

    private var heldRaw: Int16 {
        guard let index else { return 0 }
        return peakHold.peak(at: index)
    }

    var body: some View {
        GeometryReader { geometry in
            let height = geometry.size.height
            let fill = MAudio1814Level.meterPosition(raw: barRaw)
            let held = MAudio1814Level.meterPosition(raw: heldRaw)
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 2).fill(Color(white: 0.05))
                RoundedRectangle(cornerRadius: 2)
                    .fill(LinearGradient(
                        stops: [
                            .init(color: .green, location: 0),
                            .init(color: .green, location: 0.72),
                            .init(color: .yellow, location: 0.88),
                            .init(color: .red, location: 1),
                        ],
                        startPoint: .bottom, endPoint: .top))
                    .frame(height: max(0, height * fill))
                if held > 0.002 {
                    Rectangle()
                        .fill(held > 0.98 ? Color.red : Color.white)
                        .frame(height: 2)
                        .offset(y: -max(0, height * held - 2))
                }
            }
        }
        .frame(width: 13)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(name) peak meter")
        .accessibilityValue(index == nil ? "Unavailable"
                                         : "\(MAudio1814Level.formatMeter(raw: barRaw)) decibels")
    }
}
