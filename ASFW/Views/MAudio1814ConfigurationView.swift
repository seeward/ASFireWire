import SwiftUI

struct MAudio1814ConfigurationView: View {
    @StateObject private var viewModel: MAudio1814ConfigurationViewModel

    init(connector: ASFWDriverConnector) {
        _viewModel = StateObject(wrappedValue: MAudio1814ConfigurationViewModel(connector: connector))
    }

    var body: some View {
        Group {
            if let snapshot = viewModel.snapshot {
                Form {
                    Section("Committed configuration") {
                        LabeledContent("Sample rate") {
                            Text(snapshot.committed.sampleRateHz, format: .number)
                            Text(" Hz")
                        }
                        LabeledContent("Core Audio geometry") {
                            Text("\(snapshot.committed.inputChannels) in / \(snapshot.committed.outputChannels) out")
                        }
                    }
                    Section("Requested configuration") {
                        Picker("Sample rate", selection: $viewModel.selectedRateHz) {
                            ForEach(viewModel.supportedRates, id: \.self) { rate in
                                Text(rate, format: .number)
                                    .tag(rate)
                            }
                        }
                        Picker("Optical input", selection: $viewModel.selectedInputOptical) {
                            ForEach(AudioOpticalMode.allCases) { mode in
                                Text(mode.label).tag(mode)
                            }
                        }
                        Picker("Optical output", selection: $viewModel.selectedOutputOptical) {
                            ForEach(AudioOpticalMode.allCases) { mode in
                                Text(mode.label).tag(mode)
                            }
                        }
                        Button("Apply configuration", systemImage: "checkmark.circle.fill",
                               action: viewModel.apply)
                            .buttonStyle(.borderedProminent)
                            .disabled(viewModel.isApplying)
                    }
                    Section("Status") {
                        Text(viewModel.statusText)
                            .foregroundStyle(.secondary)
                    }
                }
                .formStyle(.grouped)
            } else {
                ContentUnavailableView(
                    "No FireWire 1814 configuration available",
                    systemImage: "slider.horizontal.3",
                    description: Text(viewModel.statusText)
                )
            }
        }
        .navigationTitle("FireWire 1814")
        .task { await viewModel.poll() }
    }
}
