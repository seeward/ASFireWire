import SwiftUI

/// Console presentation for a confirmed audio topology.
///
/// One vertically scrolling page with one horizontally scrolling rack. There is
/// deliberately no tab bar and no separate patchbay: every source selector now
/// sits on the strip it belongs to, which is where you are already looking when
/// you want to change it.
struct AudioTopologyDashboard: View {
    let topology: AudioTopologySnapshot
    let configuration: AudioConfigurationSnapshot
    let meters: AudioMeterSnapshot?
    let statusText: String
    let isApplying: Bool
    @Binding var selectedRate: UInt32
    @Binding var selectedInputOptical: AudioOpticalMode
    @Binding var selectedOutputOptical: AudioOpticalMode
    let supportedRates: [UInt32]
    let applyConfiguration: () -> Void
    let setMeteringEnabled: (Bool) -> Void
    let viewModel: MAudio1814ConfigurationViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                AudioTopologyHeader(configuration: configuration, meters: meters)
                AudioTopologyWaist(
                    configuration: configuration,
                    supportedRates: supportedRates,
                    selectedRate: $selectedRate,
                    selectedInputOptical: $selectedInputOptical,
                    selectedOutputOptical: $selectedOutputOptical,
                    isApplying: isApplying,
                    apply: applyConfiguration)
                AudioTopologyConsoleRack(
                    topology: topology, meters: meters,
                    peakHold: viewModel.peakHold, viewModel: viewModel)
                AudioTopologyTelemetry(meters: meters, setEnabled: setMeteringEnabled)
                Text(statusText)
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 4)
            }
            .padding(20)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

private struct AudioTopologyHeader: View {
    let configuration: AudioConfigurationSnapshot
    let meters: AudioMeterSnapshot?

    var body: some View {
        let clockLocked = meters?.isClockLocked
        HStack(spacing: 12) {
            Circle()
                .fill(clockLocked == true ? .green : clockLocked == false ? .orange : .gray)
                .frame(width: 10, height: 10)
                .shadow(color: clockLocked == true ? .green.opacity(0.8) :
                        clockLocked == false ? .orange.opacity(0.8) : .gray.opacity(0.4), radius: 4)
            VStack(alignment: .leading, spacing: 3) {
                Text("M-Audio FireWire 1814")
                    .font(.title2.bold())
                Text("CONFIRMED HARDWARE TOPOLOGY PROJECTION")
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Label("\(configuration.committed.inputChannels) Capture", systemImage: "mic.fill")
                .foregroundStyle(.cyan)
            Label("\(configuration.committed.outputChannels) Playback", systemImage: "speaker.wave.3.fill")
                .foregroundStyle(.orange)
        }
        .padding(14)
        .background(AudioTopologyCardBackground(opacity: 0.85))
    }
}

private struct AudioTopologyWaist: View {
    let configuration: AudioConfigurationSnapshot
    let supportedRates: [UInt32]
    @Binding var selectedRate: UInt32
    @Binding var selectedInputOptical: AudioOpticalMode
    @Binding var selectedOutputOptical: AudioOpticalMode
    let isApplying: Bool
    let apply: () -> Void

    var body: some View {
        AudioTopologyCard(title: "Hardware Configuration", systemImage: "waveform.path.ecg", badge: "Core Audio") {
            HStack(spacing: 18) {
                Picker("Sample Rate", selection: $selectedRate) {
                    ForEach(supportedRates, id: \.self) { Text($0.formatted() + " Hz").tag($0) }
                }
                .frame(width: 180)
                Picker("Optical In", selection: $selectedInputOptical) {
                    ForEach(AudioOpticalMode.allCases) { Text($0.label).tag($0) }
                }
                .frame(width: 155)
                Picker("Optical Out", selection: $selectedOutputOptical) {
                    ForEach(AudioOpticalMode.allCases) { Text($0.label).tag($0) }
                }
                .frame(width: 155)
                Button("Apply configuration", systemImage: "checkmark.circle.fill", action: apply)
                    .buttonStyle(.borderedProminent)
                    .disabled(isApplying)
                Spacer()
                Text("COMMITTED  \(configuration.committed.sampleRateHz.formatted()) Hz")
                    .font(.caption.monospaced().bold())
                    .foregroundStyle(.secondary)
            }
        }
    }
}

private struct AudioTopologyTelemetry: View {
    let meters: AudioMeterSnapshot?
    let setEnabled: (Bool) -> Void

    var body: some View {
        AudioTopologyCard(title: "Hardware Telemetry", systemImage: "waveform.path.ecg", badge: "HSCI") {
            HStack(spacing: 20) {
                Toggle("Enable metering", isOn: Binding(
                    get: { meters?.isEnabled ?? false }, set: setEnabled))
                .toggleStyle(.switch)
                if let meters {
                    Label(meters.isClockLocked ? "Clock locked" : "Clock not locked",
                          systemImage: meters.isClockLocked ? "lock.fill" : "lock.open")
                        .foregroundStyle(meters.isClockLocked ? .green : .orange)
                    if meters.detectedSampleRateHz != 0 {
                        Text("DEVICE \(meters.detectedSampleRateHz.formatted()) Hz")
                            .font(.caption.monospaced().bold())
                            .foregroundStyle(.secondary)
                    }
                    Label(meters.isExternallySynced ? "External sync" : "Internal clock",
                          systemImage: meters.isExternallySynced ? "link" : "clock")
                        .foregroundStyle(meters.isExternallySynced ? .cyan : .secondary)
                    Label("Switch \(meters.hardwareSwitch ? "on" : "off")",
                          systemImage: meters.hardwareSwitch ? "capsule.fill" : "capsule")
                        .foregroundStyle(meters.hardwareSwitch ? .green : .secondary)
                } else {
                    Text("Waiting for the confirmed meter snapshot…")
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            if let meters, !meters.rotaries.isEmpty {
                AudioTopologyEncoderRow(meters: meters)
            }
        }
    }
}

/// The front-panel encoders are relative: the device sends detents and the
/// driver integrates them, so these are a running total since metering was
/// enabled rather than a readback of a knob position.
private struct AudioTopologyEncoderRow: View {
    let meters: AudioMeterSnapshot

    private static let names = ["HEADPHONE 1/2", "HEADPHONE 3/4", "ASSIGNABLE"]

    var body: some View {
        HStack(spacing: 16) {
            ForEach(Array(meters.rotaries.enumerated()), id: \.offset) { index, value in
                VStack(alignment: .leading, spacing: 2) {
                    Text(index < Self.names.count ? Self.names[index] : "ENCODER \(index + 1)")
                        .font(.system(size: 9).monospaced())
                        .foregroundStyle(.secondary)
                    ProgressView(value: Double(Int(value) + 32_768) / 32_768)
                        .progressViewStyle(.linear)
                        .tint(.yellow)
                        .frame(width: 110)
                }
            }
            Spacer()
        }
    }
}

struct AudioTopologyCard<Content: View>: View {
    let title: String
    let systemImage: String
    var badge: String? = nil
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                Image(systemName: systemImage).foregroundStyle(.tint)
                Text(title).font(.headline.bold())
                if let badge {
                    Text(badge).font(.caption.bold()).padding(.horizontal, 8).padding(.vertical, 3)
                        .background(Color.secondary.opacity(0.18)).clipShape(Capsule())
                }
                Spacer()
            }
            content()
        }
        .padding(16)
        .background(AudioTopologyCardBackground(opacity: 0.7))
    }
}

struct AudioTopologyCardBackground: View {
    let opacity: Double

    var body: some View {
        RoundedRectangle(cornerRadius: 12)
            .fill(Color(nsColor: .controlBackgroundColor).opacity(opacity))
            .overlay { RoundedRectangle(cornerRadius: 12).strokeBorder(Color.white.opacity(0.08), lineWidth: 1) }
    }
}
