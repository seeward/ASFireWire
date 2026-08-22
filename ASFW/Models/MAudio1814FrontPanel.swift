import Foundation

/// Family-specific interpretation of the 1814/ProjectMix front-panel event
/// fields.  The meter transport exposes an opaque 16-bit counter; only this
/// device extension knows its detent scale and modular arithmetic.
enum MAudio1814FrontPanel {
    /// Converts adjacent wrapping counter samples into the signed movement in
    /// the device's raw level units.  At the 50 Hz hardware poll rate a move of
    /// half the counter range in one interval is physically implausible, so the
    /// signed modular delta is unambiguous in practice.
    static func rotaryDelta(current: Int16, previous: Int16) -> Int32 {
        let bits = UInt16(bitPattern: current) &- UInt16(bitPattern: previous)
        return Int32(Int16(bitPattern: bits))
    }
}
