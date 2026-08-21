import Foundation

struct AudioMeterSnapshot: Equatable, Sendable {
    let endpointID: AudioEndpointID
    let revision: UInt32
    let detectedSampleRateHz: UInt32
    let isEnabled: Bool
    let isClockLocked: Bool
    let values: [Int16]

    /// The 1814 reports 16-bit peak magnitude. The vendor UI's exact ballistics
    /// are unknown, but this conventional full-scale display matches the lab's
    /// useful -128...0 dB presentation range.
    func decibels(at index: Int) -> Double {
        guard values.indices.contains(index) else { return -128 }
        return max(-128, min(0, -128 + Double(values[index]) * 128 / 32_767))
    }
}
