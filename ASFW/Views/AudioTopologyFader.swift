import SwiftUI

/// One channel fader, positioned 0...1 by `MAudio1814Level` rather than by a
/// raw percentage — the register is linear in dB, so a linear-in-raw fader puts
/// the whole useful range in the top of the throw.
struct AudioTopologyFader: View {
    let title: String
    let position: Double
    let tint: Color
    var isEnabled = true
    let onChanged: (Double) -> Void

    var body: some View {
        AudioConsoleVerticalFader(
            title: title,
            value: position,
            range: 0...1,
            step: 0.02,
            tint: tint,
            trackWidth: 5,
            thumbSize: CGSize(width: 26, height: 16),
            fillsTrack: true,
            isEnabled: isEnabled,
            valueDescription: { "\(Int(($0 * 100).rounded())) percent" },
            onValueChanged: onChanged
        )
    }
}
