import Foundation

struct AudioMeterSnapshot: Equatable, Sendable {
    let endpointID: AudioEndpointID
    let topologyRevision: UInt64
    let telemetrySequence: UInt32
    let detectedSampleRateHz: UInt32
    let isEnabled: Bool
    let isClockLocked: Bool
    /// The device reports it is slaved to an external clock reference. Distinct
    /// from `isClockLocked`, which only says a rate was decodable at all.
    let isExternallySynced: Bool
    /// Latched state of the front-panel toggle.
    let hardwareSwitch: Bool
    /// Wrapping front-panel detent counters. These are not knob positions; use
    /// the modular difference between successive snapshots to recover motion.
    let rotaries: [Int16]
    let values: [Int16]

    /// Where each section of the 38-point peak block starts. Order is the
    /// device's, mirrored from `kMAudio1814MeterSections` in the driver.
    enum Section: String, CaseIterable, Sendable {
        case analogInput = "Analog In"
        case spdifInput = "S/PDIF In"
        case adatInput = "ADAT In"
        case analogOutput = "Analog Out"
        case spdifOutput = "S/PDIF Out"
        case adatOutput = "ADAT Out"
        case headphone = "Headphone"
        case auxOutput = "Aux Out"

        var range: Range<Int> {
            switch self {
            case .analogInput: return 0..<8
            case .spdifInput: return 8..<10
            case .adatInput: return 10..<18
            case .analogOutput: return 18..<22
            case .spdifOutput: return 22..<24
            case .adatOutput: return 24..<32
            case .headphone: return 32..<36
            case .auxOutput: return 36..<38
            }
        }
    }

    func peaks(in section: Section) -> [Int16] {
        section.range.compactMap { values.indices.contains($0) ? values[$0] : nil }
    }

    /// The 1814 reports 16-bit peak magnitude. The vendor UI's exact ballistics
    /// are unknown, but this conventional full-scale display matches the lab's
    /// useful -128...0 dB presentation range.
    func decibels(at index: Int) -> Double {
        guard values.indices.contains(index) else { return -128 }
        return max(-128, min(0, -128 + Double(values[index]) * 128 / 32_767))
    }
}
