import SwiftUI

struct DuetConsoleView: View {
    let console: DuetConsoleSnapshot
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Hardware Monitor Mixer")
                        .font(.title2.weight(.semibold))
                    Text("Input 1/2 and DAW playback feed the stereo Duet cue mixer.")
                        .foregroundStyle(.secondary)
                }

                DuetConsoleCard(title: "Inputs") {
                    HStack(alignment: .top, spacing: 12) {
                        ForEach(console.inputs) { input in
                            DuetInputStrip(input: input, isWriting: isWriting, submit: submit)
                        }
                    }
                }

                DuetConsoleCard(title: "Cue Mixer") {
                    ScrollView(.horizontal) {
                        HStack(alignment: .top, spacing: 12) {
                            ForEach(console.mixerSources) { source in
                                DuetMixerSourceStrip(source: source, isWriting: isWriting, submit: submit)
                            }
                            Divider().frame(height: 250)
                            DuetMainOutputStrip(output: console.mainOutput, isWriting: isWriting, submit: submit)
                        }
                    }
                    .scrollIndicators(.hidden)
                }
            }
            .frame(maxWidth: 920, alignment: .leading)
            .padding()
        }
    }
}
