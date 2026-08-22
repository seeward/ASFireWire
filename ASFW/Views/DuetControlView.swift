import SwiftUI

struct DuetControlView: View {
    @StateObject private var viewModel: DuetConfigurationViewModel

    init(connector: ASFWDriverConnector) {
        _viewModel = StateObject(wrappedValue: DuetConfigurationViewModel(connector: connector))
    }

    var body: some View {
        Group {
            if let console = viewModel.console {
                DuetConsoleView(
                    console: console,
                    configuration: viewModel.configuration,
                    selectedRateHz: $viewModel.selectedRateHz,
                    isApplyingConfiguration: viewModel.isApplyingConfiguration,
                    applySampleRate: viewModel.applySampleRate,
                    isWriting: viewModel.writingParameterIDs.contains,
                    submit: { control, value in viewModel.submit(control.parameter, value: value) },
                    metersEnabled: viewModel.metersEnabled,
                    setMetersEnabled: viewModel.setMeteringEnabled
                )
            } else {
                ContentUnavailableView(
                    "Duet console is not ready",
                    systemImage: "slider.horizontal.3",
                    description: Text(viewModel.statusText)
                )
            }
        }
        .navigationTitle("Duet")
        .task { await viewModel.poll() }
    }

}
