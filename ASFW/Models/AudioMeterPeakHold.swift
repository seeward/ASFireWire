import Foundation

/// Peak-marker ballistics over the driver's meter snapshots.
///
/// **The bar itself is not derived here.** The device already maintains an
/// envelope: two block reads 22 ms apart come back byte-identical, so the value
/// it reports holds across polls rather than being a sample of the waveform.
/// Drawing it directly is both simpler and more honest than smoothing something
/// that is already smooth — an earlier version ran a release envelope over it
/// and that was a second ballistic on top of the device's own.
///
/// What the device does *not* provide is a held peak, so that is all this keeps:
/// a marker that rises instantly with the signal, stays pinned long enough to be
/// read, then falls at a fixed rate in dB.
///
/// The hold and fall rates are conventional meter ballistics — a display choice,
/// not a measurement. Nothing in the hardware specifies them.
struct AudioMeterPeakHold: Equatable {
    /// How long a peak stays pinned before it starts to fall.
    static let holdSeconds: Double = 1.5
    /// Fall rate once the hold expires.
    static let decayDbPerSecond: Double = 12

    private var peaks: [Int16] = []
    private var pinnedAt: [Double] = []
    private var lastObservation: Double?

    mutating func observe(_ snapshot: AudioMeterSnapshot,
                          now: Double = ProcessInfo.processInfo.systemUptime) {
        let count = snapshot.values.count
        guard count > 0 else { return }
        if peaks.count != count {
            peaks = snapshot.values
            pinnedAt = Array(repeating: now, count: count)
            lastObservation = now
            return
        }

        let elapsed = max(0, now - (lastObservation ?? now))
        lastObservation = now

        for index in 0..<count {
            let sample = snapshot.values[index]
            if sample >= peaks[index] {
                peaks[index] = sample
                pinnedAt[index] = now
                continue
            }
            guard now - pinnedAt[index] > Self.holdSeconds else { continue }
            let decayed = MAudio1814Level.meterDecibels(raw: peaks[index])
                - Self.decayDbPerSecond * elapsed
            let floor = MAudio1814Level.meterDecibels(raw: sample)
            peaks[index] = MAudio1814Level.rawMeter(decibels: max(decayed, floor))
        }
    }

    func peak(at index: Int) -> Int16 {
        peaks.indices.contains(index) ? peaks[index] : 0
    }

    mutating func reset() {
        peaks.removeAll()
        pinnedAt.removeAll()
        lastObservation = nil
    }
}
