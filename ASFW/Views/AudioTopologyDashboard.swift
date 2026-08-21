import SwiftUI

/// Generic console and patchbay presentation for a confirmed audio topology.
/// Device-specific code supplies an `AudioTopologySnapshot` and command
/// closures; this view deliberately has no knowledge of M-Audio.
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
    let setLevel: (MAudio1814ControlID, Double) -> Void
    let setSend: (AudioTopologySend, Bool) -> Void
    let setRoute: (MAudio1814ControlID, Int32) -> Void
    let setMeteringEnabled: (Bool) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
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
                    setLevel: setLevel, setSend: setSend)
                AudioTopologyPatchbay(topology: topology, setRoute: setRoute)
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
                } else {
                    Text("Waiting for the confirmed meter snapshot…")
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
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
