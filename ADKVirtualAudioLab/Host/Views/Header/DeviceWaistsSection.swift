import SwiftUI

struct DeviceWaistsSection: View {
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    private var clockSourceParam: ParameterModel? {
        snap.parameters.first { $0.semantic == ASFW_SEMANTIC_CLOCK_SOURCE }
    }

    var body: some View {
        VStack(spacing: 12) {
            HStack(spacing: 18) {
                // Sample Rate Control
                HStack(spacing: 8) {
                    Image(systemName: "waveform.path.ecg")
                        .foregroundStyle(.tint)
                    Text("Sample Rate:")
                        .font(.subheadline)
                        .fontWeight(.semibold)

                    Picker("", selection: Binding(
                        get: { snap.currentSampleRate },
                        set: { state.setSampleRate($0) }
                    )) {
                        ForEach(snap.supportedSampleRates, id: \.self) { rate in
                            Text("\(rate) Hz").tag(rate)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 125)
                }

                // Clock Source Control
                if let clk = clockSourceParam, !clk.enumItems.isEmpty {
                    Divider().frame(height: 20)

                    HStack(spacing: 8) {
                        Image(systemName: "clock.badge.checkmark")
                            .foregroundStyle(.green)
                        Text("Clock Source:")
                            .font(.subheadline)
                            .fontWeight(.semibold)

                        Picker("", selection: Binding(
                            get: { clk.enumValue },
                            set: { state.setParameterEnum(id: clk.id, value: $0) }
                        )) {
                            ForEach(clk.enumItems) { item in
                                Text(item.name).tag(item.value)
                            }
                        }
                        .labelsHidden()
                        .frame(minWidth: 130, maxWidth: 210)
                    }
                }

                // Optical Modes
                if snap.hasOptical {
                    Divider().frame(height: 20)

                    HStack(spacing: 8) {
                        Image(systemName: "point.3.connected.trianglepath.dotted")
                            .foregroundStyle(.purple)
                        Text("Optical In:")
                            .font(.subheadline)

                        Picker("", selection: Binding(
                            get: { snap.opticalInput },
                            set: { state.setOpticalMode(input: $0, output: snap.opticalOutput) }
                        )) {
                            Text("ADAT (8 ch)").tag(ASFW_OPTICAL_ADAT)
                            Text("S/PDIF (2 ch)").tag(ASFW_OPTICAL_SPDIF)
                        }
                        .labelsHidden()
                        .frame(width: 125)
                    }

                    HStack(spacing: 8) {
                        Text("Optical Out:")
                            .font(.subheadline)

                        Picker("", selection: Binding(
                            get: { snap.opticalOutput },
                            set: { state.setOpticalMode(input: snap.opticalInput, output: $0) }
                        )) {
                            Text("ADAT (8 ch)").tag(ASFW_OPTICAL_ADAT)
                            Text("S/PDIF (2 ch)").tag(ASFW_OPTICAL_SPDIF)
                        }
                        .labelsHidden()
                        .frame(width: 125)
                    }
                }

                Spacer()

                // Active Stream Channels
                HStack(spacing: 14) {
                    Label("\(snap.totalCaptureChannels) Capture Channels", systemImage: "mic.fill")
                        .font(.subheadline.bold())
                        .foregroundStyle(.cyan)

                    Text("•")
                        .foregroundStyle(.secondary)

                    Label("\(snap.totalPlaybackChannels) Playback Channels", systemImage: "speaker.wave.3.fill")
                        .font(.subheadline.bold())
                        .foregroundStyle(.orange)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(Color.secondary.opacity(0.12))
                .clipShape(Capsule())
            }

            // Architecture Metrics Bar
            HStack(spacing: 20) {
                Label("\(snap.nodes.count) Audio Nodes", systemImage: "square.grid.2x2")
                Label("\(snap.ports.count) Stream Ports", systemImage: "circle.circle")
                Label("\(snap.linkCount) Fixed Links", systemImage: "arrow.right.circle")
                Label("\(snap.parameters.count) Parameters", systemImage: "slider.horizontal.3")
                Label("\(snap.routers.count) Routers", systemImage: "point.3.filled.connected.trianglepath.dotted")
                Label("\(snap.mixers.count) Mixers", systemImage: "slider.vertical.3")
                Spacer()
                Text("DriverKit C++ Runtime")
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
            }
            .font(.caption)
            .foregroundStyle(.secondary)
        }
        .padding(14)
        .background(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.45))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(Color.white.opacity(0.06), lineWidth: 1)
        )
    }
}
