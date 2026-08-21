import Foundation

/// The 1814's level scale, and the only place raw register values become
/// decibels or fader travel.
///
/// The device's gain and volume registers are **linear in decibels**, not in
/// amplitude: the i16 range -32768...0 spans -128...0 dB at exactly 1 dB per
/// 0x100 step. Two independent sources say so —
///
///   - the ALSA crate declares `GAIN_TLV` / `VOLUME_TLV` as
///     `DbInterval { min: -12800, max: 0, linear: false }`, and ALSA TLV units
///     are 0.01 dB, so -12800 is -128.00 dB. Its `GAIN_STEP` of 0x100 is
///     therefore one decibel.
///   - the device's own shell reports `fw vol` as "Range -128...0 dB"
///     (documentation/1814.md §8).
///
/// Meters use a wider scale: the crate's `METER_TLV` is -14400, i.e. -144 dB.
enum MAudio1814Level {
    /// dB at the bottom of the register range. Treated as silence.
    static let minimumDb: Double = -128
    static let maximumDb: Double = 0
    /// Register units per decibel. `MaudioSpecialInputProtocol::GAIN_STEP`.
    static let unitsPerDb: Double = 256

    static let rawMinimum: Int32 = -32768
    static let rawMaximum: Int32 = 0

    static func decibels(raw: Int32) -> Double {
        Double(raw) / unitsPerDb
    }

    static func raw(decibels: Double) -> Int32 {
        let clamped = max(minimumDb, min(maximumDb, decibels))
        return Int32((clamped * unitsPerDb).rounded())
    }

    // MARK: - Fader travel

    /// Decibels at the bottom of the fader's *usable* travel.
    ///
    /// The register bottoms out at -128 dB, but spreading that over the whole
    /// fader puts everything above -20 dB — which is all anyone adjusts — in the
    /// top 15% of the throw. The vendor's own scale stops marking at -50 dB and
    /// labels the bottom as -∞, so the travel covers 0...-60 dB and the last
    /// step snaps to silence.
    ///
    /// This is our choice of taper, not a measurement: the vendor's fader law is
    /// only observable as pixel spacing in a screenshot, which is not evidence.
    /// Linear-in-dB is the honest default and matches the declared TLV.
    static let faderFloorDb: Double = -60

    /// Fader position, 0 at the bottom of travel and 1 at unity.
    static func position(raw: Int32) -> Double {
        let db = decibels(raw: raw)
        guard db > faderFloorDb else { return 0 }
        return (db - faderFloorDb) / (maximumDb - faderFloorDb)
    }

    static func raw(position: Double) -> Int32 {
        let clamped = max(0, min(1, position))
        // The very bottom of the throw is silence, not -60 dB.
        guard clamped > 0 else { return rawMinimum }
        return raw(decibels: faderFloorDb + clamped * (maximumDb - faderFloorDb))
    }

    /// Tick marks for a fader, in dB. Mirrors the vendor's scale.
    static let faderTicksDb: [Double] = [0, -2, -5, -10, -20, -30, -50]

    /// Formats a level for display. Anything at or below the fader floor reads
    /// as silence rather than a misleadingly precise number.
    static func format(raw: Int32) -> String {
        let db = decibels(raw: raw)
        if db <= faderFloorDb { return "-∞" }
        if db <= -10 { return String(format: "%.0f", db) }
        return String(format: "%.1f", db)
    }

    // MARK: - Metering

    /// Meters are a **different scale from the gain registers above**, and
    /// conflating the two is what made every meter read empty.
    ///
    /// Gains are linear in dB. Meters are a **linear amplitude** magnitude over
    /// 0...i16.max, so decibels are logarithmic: `20·log10(raw / 32767)`.
    ///
    /// Measured, not inferred. A FireBug capture of the block during playback:
    ///
    ///     analog out 1/2   0x37f7 0x39b4
    ///     headphone 1/2    0x37f7 0x39b4
    ///     aux out 1/2      0x37f7 0x39b4
    ///
    /// 0x37f7 is 43.7% of full scale. Read as amplitude that is -7.2 dBFS, which
    /// is what a playing signal looks like; read as a linear dB scale it would be
    /// -81 dB, which is not.
    ///
    /// **The ALSA crate's `METER_TLV` (`DbInterval { min: -14400, linear: false }`,
    /// i.e. linear-in-dB) does not match the hardware.** Its *gain* TLV does, and
    /// is corroborated by the device's own shell; only the meter one is wrong.
    /// Where a reference and a measurement disagree, the measurement wins.
    static let meterFloorDb: Double = -90  // 20·log10(1/32767) ≈ -90.3

    static func meterDecibels(raw: Int16) -> Double {
        let magnitude = Double(max(0, Int(raw)))
        guard magnitude > 0 else { return meterFloorDb }
        return max(meterFloorDb, 20 * log10(magnitude / Double(Int16.max)))
    }

    /// Meter fill, 0...1, over the range worth showing rather than the full
    /// -144 dB — otherwise a healthy signal barely moves the bar.
    static let meterDisplayFloorDb: Double = -60

    static func meterPosition(raw: Int16) -> Double {
        let db = meterDecibels(raw: raw)
        guard db > meterDisplayFloorDb else { return 0 }
        return min(1, (db - meterDisplayFloorDb) / (0 - meterDisplayFloorDb))
    }

    static func formatMeter(raw: Int16) -> String {
        let db = meterDecibels(raw: raw)
        if db <= meterDisplayFloorDb { return "-∞" }
        return String(format: "%.0f", db)
    }

    /// Inverse of `meterDecibels`, for the peak marker's decay.
    static func rawMeter(decibels: Double) -> Int16 {
        guard decibels > meterFloorDb else { return 0 }
        let scaled = Double(Int16.max) * pow(10, min(0, decibels) / 20)
        return Int16(max(0, min(Double(Int16.max), scaled.rounded())))
    }

    // MARK: - Pan

    /// Pan registers are centred at zero and hard-panned at ±32640 — the
    /// vendor's ±255 domain through `SetInputPan`'s -128 multiplier.
    static let panExtent: Int32 = 32640

    /// Pan as -1 (hard left) ... +1 (hard right).
    ///
    /// **A positive register value is LEFT.** The vendor's factory default puts
    /// the first channel of each pair at +32640 and its panel prints `L127`
    /// under that channel's knob, so the sign is inverted relative to the
    /// display. This also settles the polarity disagreement noted in the driver:
    /// the kext's default hard-pans channel one left, which is correct, and the
    /// ALSA crate's `[MIN, MAX]` default is the one that is backwards.
    static func panPosition(raw: Int32) -> Double {
        max(-1, min(1, -Double(raw) / Double(panExtent)))
    }

    static func rawPan(position: Double) -> Int32 {
        let clamped = max(-1, min(1, position))
        return Int32((-clamped * Double(panExtent)).rounded())
    }

    /// The vendor prints pan as `L127` / `R127`; its display domain is half the
    /// ±255 register domain.
    static func formatPan(raw: Int32) -> String {
        let steps = Int((panPosition(raw: raw) * 127).rounded())
        if steps == 0 { return "C" }
        return steps < 0 ? "L\(-steps)" : "R\(steps)"
    }
}
