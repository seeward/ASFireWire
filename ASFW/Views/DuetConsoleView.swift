import SwiftUI

struct DuetConsoleView: View {
    let console: DuetConsoleSnapshot
    let configuration: AudioConfigurationSnapshot?
    @Binding var selectedRateHz: UInt32
    let isApplyingConfiguration: Bool
    let applySampleRate: () -> Void
    let isWriting: (UInt32) -> Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void
    let metersEnabled: Bool
    let setMetersEnabled: (Bool) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                if let configuration {
                    AudioTopologyCard(title: "Hardware Configuration", systemImage: "waveform", badge: "Core Audio") {
                        HStack(spacing: 16) {
                            DuetSampleRateControl(
                                committedRateHz: configuration.committed.sampleRateHz,
                                selectedRateHz: $selectedRateHz,
                                isApplying: isApplyingConfiguration,
                                apply: applySampleRate
                            )
                            Spacer(minLength: 12)
                            Toggle("Enable metering", isOn: Binding(get: { metersEnabled }, set: setMetersEnabled))
                                .toggleStyle(.switch)
                        }
                    }
                }

                AudioTopologyCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Duet") {
                    ScrollView(.horizontal, showsIndicators: true) {
                        HStack(alignment: .top, spacing: 14) {
                            AudioConsoleRackBank(title: "INPUTS", tint: .cyan, accessory: {
                                DuetStereoLinkButton(
                                    control: console.stereoLink,
                                    isWriting: isWriting(console.stereoLink.id),
                                    submit: submit
                                )
                            }) {
                                ForEach(console.inputs) { input in
                                    DuetInputStrip(input: input, isWriting: isWriting, submit: submit)
                                }
                            }
                            AudioConsoleRackDivider()
                            AudioConsoleRackBank(title: "CUE MATRIX · SOURCE → L / R", tint: .purple) {
                                ForEach(console.mixerStrips) { source in
                                    DuetMixerSourceStrip(source: source, isWriting: isWriting, submit: submit)
                                }
                            }
                            AudioConsoleRackDivider()
                            AudioConsoleRackBank(title: "OUTPUT", tint: .orange) {
                                DuetMainOutputStrip(output: console.mainOutput, isWriting: isWriting, submit: submit)
                            }
                        }
                        .padding(.vertical, 4)
                    }
                }
            }
            .padding()
        }
    }
}

private struct DuetStereoLinkButton: View {
    let control: DuetConsoleSnapshot.Control
    let isWriting: Bool
    let submit: (DuetConsoleSnapshot.Control, Int32) -> Void

    var body: some View {
        DuetConsoleToggle(
            title: "STEREO LINK",
            accessibilityLabel: "Input stereo link",
            isOn: control.value != 0,
            isWriting: isWriting,
            action: { submit(control, control.value == 0 ? control.parameter.maximum : control.parameter.minimum) }
        )
        .frame(width: 92)
    }
}
