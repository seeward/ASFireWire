import SwiftUI

/// The meter registers provide real left/right values.  Keep them separate:
/// taking the maximum hid a silent or misrouted channel.
struct DuetStereoMeters: View {
    let levels: [Int16]
    let label: String

    var body: some View {
        HStack(alignment: .bottom, spacing: 3) {
            meter(channel: "L", value: levels[safe: 0] ?? 0)
            meter(channel: "R", value: levels[safe: 1] ?? 0)
        }
    }

    private func meter(channel: String, value: Int16) -> some View {
        VStack(spacing: 3) {
            Text(channel)
                .font(.system(size: 9, weight: .bold).monospaced())
                .foregroundStyle(.secondary)
            DuetLevelMeter(value: value, label: "\(label) \(channel)")
        }
    }
}

private extension Collection {
    subscript(safe index: Index) -> Element? { indices.contains(index) ? self[index] : nil }
}
