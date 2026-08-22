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
        AudioConsoleMeter(
            level: MAudio1814Level.meterPosition(raw: barRaw),
            heldPeak: index == nil ? nil : MAudio1814Level.meterPosition(raw: heldRaw),
            label: "\(name) peak meter",
            valueDescription: index == nil
                ? "Unavailable"
                : "\(MAudio1814Level.formatMeter(raw: barRaw)) decibels"
        )
    }
}
