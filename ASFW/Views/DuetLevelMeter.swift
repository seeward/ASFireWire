import SwiftUI

/// Adapts Duet's 14-bit peak values to a standard -60...0 dBFS visual scale.
/// The protocol owns polling; this adapter only presents an already sampled
/// magnitude and never synthesizes meter activity.
struct DuetLevelMeter: View {
    let value: Int16
    let label: String

    var body: some View {
        AudioConsoleMeter(
            level: meterPosition,
            heldPeak: nil,
            label: label,
            valueDescription: valueText,
            width: 12,
            height: 168
        )
    }

    private static let maximumValue: Int16 = 0x3fff
    private static let displayFloorDb = -60.0

    private var decibels: Double? {
        let magnitude = Double(max(0, Int(value)))
        guard magnitude > 0 else { return nil }
        return max(Self.displayFloorDb, 20 * log10(magnitude / Double(Self.maximumValue)))
    }

    private var meterPosition: Double {
        guard let decibels else { return 0 }
        return (decibels - Self.displayFloorDb) / -Self.displayFloorDb
    }

    private var valueText: String {
        guard let decibels else { return "−∞ dBFS" }
        return "\(Int(decibels.rounded())) dBFS"
    }
}
