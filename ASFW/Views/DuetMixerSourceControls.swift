import SwiftUI

/// One mono source row in Duet's fixed 4×2 cue matrix. The two faders are its
/// contributions to the cue mix's left and right outputs, not a second stereo
/// source hidden inside the strip.
struct DuetMixerSourceControls: View {
    let source: DuetConsoleSnapshot.MixerStrip
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        HStack(alignment: .bottom, spacing: 6) {
            ForEach(source.sends) { send in
                VStack(spacing: 5) {
                    Text(send.destinationName)
                        .font(.caption2.weight(.bold))
                        .foregroundStyle(.secondary)
                    DuetVerticalFader(
                        title: "\(source.name) send to cue \(send.destinationName)",
                        control: send.level,
                        isWriting: isWriting(send.level.id),
                        submit: { submit(send.level, $0) }
                    )
                    DuetControlReadout(control: send.level)
                }
            }
            if let scaleControl = source.sends.first?.level {
                DuetFaderScale(control: scaleControl)
            }
            DuetStereoMeters(levels: source.meterLevels, label: source.name)
            DuetMeterScale()
        }
    }
}
