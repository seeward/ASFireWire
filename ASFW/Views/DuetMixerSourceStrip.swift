import SwiftUI

struct DuetMixerSourceStrip: View {
    let source: DuetConsoleSnapshot.MixerSource
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        VStack(spacing: 10) {
            Text(source.name.uppercased())
                .font(.caption.weight(.bold))
                .foregroundStyle(source.name.contains("DAW") ? Color.purple : Color.cyan)
                .lineLimit(1)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 5)
                .background(Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 6, style: .continuous))

            HStack(alignment: .top, spacing: 8) {
                ForEach(source.sends) { send in
                    VStack(spacing: 6) {
                        Text(send.destinationName.replacingOccurrences(of: "Mixer ", with: ""))
                            .font(.caption2.weight(.bold))
                            .foregroundStyle(.secondary)
                        DuetVerticalFader(
                            title: "\(source.name) send to \(send.destinationName)",
                            control: send.level,
                            isWriting: isWriting(send.level.id),
                            submit: { submit(send.level, $0) }
                        )
                        DuetControlReadout(control: send.level)
                    }
                }
            }
        }
        .padding(10)
        .background(.tertiary.opacity(0.55), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }
}
