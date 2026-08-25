import SwiftUI

/// Readback rows intentionally keep physical I/O, the monitor mixer, DSP, and
/// patching distinct. That matches the hardware's signal graph rather than the
/// old MixControl tab layout.
struct SaffireConsoleStatusCard: View {
    let status: String
    let controls: SaffireControlSurface?

    var body: some View {
        AudioTopologyCard(title: "Hardware Audio Console", systemImage: "slider.vertical.3", badge: "Saffire") {
            HStack(alignment: .firstTextBaseline, spacing: 18) {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Saffire Pro 24 DSP")
                        .font(.title3.bold())
                    Text("Physical I/O · monitor mixer · DSP · patchbay")
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Label(controls == nil ? "Control readback pending" : "Control readback current",
                      systemImage: controls == nil ? "clock" : "checkmark.circle.fill")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(controls == nil ? Color.secondary : Color.green)
            }
            Text(status)
                .font(.caption.monospaced())
                .foregroundStyle(.secondary)
        }
    }
}

struct SaffireInputOutputSection: View {
    let endpointID: AudioEndpointID
    let connector: ASFWDriverConnector
    let controls: SaffireControlSurface?
    @State private var writeInFlight = false
    @State private var writeStatus: String?

    var body: some View {
        HStack(alignment: .top, spacing: 18) {
            AudioTopologyCard(title: "Physical Inputs", systemImage: "mic.fill", badge: "Hardware") {
                if let controls {
                    HStack(alignment: .top, spacing: 8) {
                        inputCard("INPUT 1", value: controls.micInputModes[0].label,
                                  controlID: SaffireControlID.micInputMode1,
                                  nextValue: controls.micInputModes[0] == .line ? 1 : 0)
                        inputCard("INPUT 2", value: controls.micInputModes[1].label,
                                  controlID: SaffireControlID.micInputMode2,
                                  nextValue: controls.micInputModes[1] == .line ? 1 : 0)
                        inputCard("LINE 3/4", value: controls.lineInputLevels[0].label,
                                  controlID: SaffireControlID.lineInputLevel34,
                                  nextValue: controls.lineInputLevels[0] == .low ? 1 : 0)
                        inputCard("LINE 5/6", value: controls.lineInputLevels[1].label,
                                  controlID: SaffireControlID.lineInputLevel56,
                                  nextValue: controls.lineInputLevels[1] == .low ? 1 : 0)
                    }
                    controlStatus
                } else {
                    SaffirePendingState(message: "Reading the input-parameter block…")
                }
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)

            AudioTopologyCard(title: "Physical Outputs", systemImage: "speaker.wave.3.fill", badge: "Hardware") {
                if let controls {
                    VStack(alignment: .leading, spacing: 7) {
                        HStack(spacing: 7) {
                            actionButton(title: "GLOBAL MUTE", isOn: controls.globalMute, tint: .orange) {
                                submit(SaffireControlID.globalMute, controls.globalMute ? 0 : 1)
                            }
                            actionButton(title: "DIM", isOn: controls.globalDim, tint: .orange) {
                                submit(SaffireControlID.globalDim, controls.globalDim ? 0 : 1)
                            }
                        }
                        ForEach(controls.outputPairs) { pair in
                            SaffireOutputPairRow(pair: pair, isEnabled: !writeInFlight) { lane, value in
                                submit(SaffireControlID.outputVolumeFirst + UInt32(pair.id * 2 + lane), value)
                            } onMute: { lane, muted in
                                submit(SaffireControlID.outputMuteFirst + UInt32(pair.id * 2 + lane), muted ? 1 : 0)
                            }
                        }
                        Text("Volume is the device's 0…127 logical output control; its calibrated dB law is not published yet.")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                        controlStatus
                    }
                } else {
                    SaffirePendingState(message: "Reading output groups and mute state…")
                }
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
        }
    }

    private func inputCard(_ title: String, value: String, controlID: UInt32, nextValue: Int32) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title)
                .font(.caption.monospaced().bold())
                .foregroundStyle(.cyan)
            Button(value) { submit(controlID, nextValue) }
                .buttonStyle(.bordered)
                .controlSize(.small)
                .disabled(writeInFlight)
            Text("hardware mode")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .padding(9)
        .frame(minWidth: 104, alignment: .leading)
        .background(Color.cyan.opacity(0.1), in: RoundedRectangle(cornerRadius: 7))
    }

    @ViewBuilder
    private var controlStatus: some View {
        if let writeStatus {
            Text(writeStatus)
                .font(.caption2.monospaced())
                .foregroundStyle(writeStatus.hasPrefix("Could not") ? .red : .secondary)
        }
    }

    private func actionButton(title: String, isOn: Bool, tint: Color,
                              action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text("\(title)  \(isOn ? "ON" : "OFF")")
                .font(.caption2.bold())
                .foregroundStyle(isOn ? Color.black : Color.secondary)
                .padding(.horizontal, 7)
                .padding(.vertical, 4)
                .background(isOn ? tint : Color.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 4))
        }
        .buttonStyle(.plain)
        .disabled(writeInFlight)
        .accessibilityLabel(title)
        .accessibilityValue(isOn ? "On" : "Off")
    }

    private func submit(_ controlID: UInt32, _ value: Int32) {
        guard !writeInFlight else { return }
        writeInFlight = true
        writeStatus = "Applying hardware control…"
        connector.submitAudioControlValue(endpointID: endpointID, controlID: controlID, value: value) { result in
            Task { @MainActor in
                writeInFlight = false
                writeStatus = result == KERN_SUCCESS
                    ? "Hardware write confirmed."
                    : "Could not apply control: \(connector.interpretIOReturn(result))"
            }
        }
    }
}

struct SaffireDspSection: View {
    let controls: SaffireControlSurface?

    var body: some View {
        AudioTopologyCard(title: "DSP", systemImage: "waveform.path.ecg", badge: "Readback") {
            if let controls {
                HStack(alignment: .top, spacing: 12) {
                    ForEach(controls.channelStrips) { strip in
                        SaffireDspStrip(strip: strip)
                    }
                    SaffireDspEffectCard(
                        title: "STEREO REVERB",
                        detail: "hardware effect",
                        isOn: controls.reverbEnabled,
                        tint: .purple)
                    SaffireDspEffectCard(
                        title: "VRM / INSITU",
                        detail: "alternate DSP path",
                        isOn: controls.inSituEnabled,
                        tint: .yellow)
                    Spacer(minLength: 0)
                }
                Text("Compressor and reverb coefficient values are retained in the driver; their controls stay hidden until their calibrated parameter mapping and write/readback path are verified.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                SaffirePendingState(message: "Reading channel-strip and DSP state…")
            }
        }
    }
}

struct SaffirePatchbaySection: View {
    let inputs: [AudioSemanticMatrixSnapshot.Axis]

    var body: some View {
        AudioTopologyCard(title: "Patchbay", systemImage: "point.3.connected.trianglepath.dotted", badge: "Active mixer inputs") {
            Text("These are the active router assignments feeding the monitor mixer. They are not mixer gains and they are not physical-output routes.")
                .font(.caption)
                .foregroundStyle(.secondary)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 154), spacing: 7)], alignment: .leading, spacing: 7) {
                ForEach(Array(inputs.enumerated()), id: \.element.id) { index, axis in
                    HStack(spacing: 6) {
                        Text("MIX IN \(index + 1)")
                            .font(.caption2.monospaced().bold())
                            .foregroundStyle(.secondary)
                        Image(systemName: "arrow.left")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                        Text(SaffireSignalLabel.title(axis))
                            .font(.caption.monospaced().bold())
                            .foregroundStyle(SaffireSignalLabel.tint(axis))
                            .lineLimit(1)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(Color.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 5))
                }
            }
            Text("Patchbay writing stays unavailable until a single-route mutation and exact vendor commit/readback sequence are verified through MCP.")
                .font(.caption.monospaced())
                .foregroundStyle(.secondary)
        }
    }
}

private struct SaffireOutputPairRow: View {
    let pair: SaffireControlSurface.OutputPair
    let isEnabled: Bool
    let onVolume: (Int, Int32) -> Void
    let onMute: (Int, Bool) -> Void

    var body: some View {
        HStack(spacing: 8) {
            Text(pair.title)
                .font(.caption.monospaced().bold())
                .foregroundStyle(.orange)
                .frame(width: 158, alignment: .leading)
            SaffireOutputLane(title: "L", volume: pair.leftVolume, muted: pair.leftMuted,
                              isEnabled: isEnabled,
                              onVolume: { onVolume(0, $0) }, onMute: { onMute(0, $0) })
            SaffireOutputLane(title: "R", volume: pair.rightVolume, muted: pair.rightMuted,
                              isEnabled: isEnabled,
                              onVolume: { onVolume(1, $0) }, onMute: { onMute(1, $0) })
        }
    }
}

private struct SaffireOutputLane: View {
    let title: String
    let volume: Int32
    let muted: Bool
    let isEnabled: Bool
    let onVolume: (Int32) -> Void
    let onMute: (Bool) -> Void

    var body: some View {
        HStack(spacing: 4) {
            Text(title).font(.caption2.monospaced().bold()).foregroundStyle(.secondary)
            Button { onVolume(max(0, volume - 1)) } label: { Image(systemName: "minus") }
                .buttonStyle(.borderless).controlSize(.mini).disabled(!isEnabled || volume == 0)
            Text("\(volume)").font(.caption.monospaced().bold()).frame(minWidth: 23)
            Button { onVolume(min(127, volume + 1)) } label: { Image(systemName: "plus") }
                .buttonStyle(.borderless).controlSize(.mini).disabled(!isEnabled || volume == 127)
            Button(muted ? "MUTE" : "mute") { onMute(!muted) }
                .buttonStyle(.borderless).controlSize(.mini).disabled(!isEnabled)
                .foregroundStyle(muted ? .red : .secondary)
        }
        .frame(minWidth: 130, alignment: .leading)
    }
}

private struct SaffireDspStrip: View {
    let strip: SaffireControlSurface.ChannelStrip

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("CHANNEL STRIP \(strip.id + 1)")
                .font(.caption.monospaced().bold())
                .foregroundStyle(.yellow)
            SaffireStatePill(title: "EQ", isOn: strip.equalizerEnabled, tint: .yellow)
            SaffireStatePill(title: "COMP", isOn: strip.compressorEnabled, tint: .yellow)
            Text(strip.equalizerAfterCompressor ? "EQ after compressor" : "EQ before compressor")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .padding(10)
        .frame(width: 160, alignment: .leading)
        .background(Color.yellow.opacity(0.09), in: RoundedRectangle(cornerRadius: 8))
    }
}

private struct SaffireDspEffectCard: View {
    let title: String
    let detail: String
    let isOn: Bool
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.caption.monospaced().bold())
                .foregroundStyle(tint)
            SaffireStatePill(title: "ENABLE", isOn: isOn, tint: tint)
            Text(detail).font(.caption2).foregroundStyle(.secondary)
        }
        .padding(10)
        .frame(width: 160, alignment: .leading)
        .background(tint.opacity(0.09), in: RoundedRectangle(cornerRadius: 8))
    }
}

private struct SaffireStatePill: View {
    let title: String
    let isOn: Bool
    let tint: Color

    var body: some View {
        Text("\(title)  \(isOn ? "ON" : "OFF")")
            .font(.caption2.bold())
            .foregroundStyle(isOn ? Color.black : Color.secondary)
            .padding(.horizontal, 7)
            .padding(.vertical, 4)
            .background(isOn ? tint : Color.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 4))
            .accessibilityLabel(title)
            .accessibilityValue(isOn ? "On" : "Off")
    }
}

private struct SaffirePendingState: View {
    let message: String

    var body: some View {
        Label(message, systemImage: "clock")
            .font(.caption)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, minHeight: 66, alignment: .center)
    }
}
