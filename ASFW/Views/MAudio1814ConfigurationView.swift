import SwiftUI

struct MAudio1814ConfigurationView: View {
    @StateObject private var viewModel: MAudio1814ConfigurationViewModel

    init(connector: ASFWDriverConnector) {
        _viewModel = StateObject(wrappedValue: MAudio1814ConfigurationViewModel(connector: connector))
    }

    var body: some View {
        Group {
            if let snapshot = viewModel.snapshot, let topology = viewModel.topology {
                AudioTopologyDashboard(
                    topology: topology,
                    configuration: snapshot,
                    meters: viewModel.meterSnapshot,
                    statusText: viewModel.statusText,
                    isApplying: viewModel.isApplying,
                    selectedRate: $viewModel.selectedRateHz,
                    selectedInputOptical: $viewModel.selectedInputOptical,
                    selectedOutputOptical: $viewModel.selectedOutputOptical,
                    supportedRates: viewModel.supportedRates,
                    applyConfiguration: viewModel.apply,
                    setLevel: viewModel.setTopologyLevel,
                    setWidth: viewModel.setTopologyWidth,
                    setSend: viewModel.setTopologySend,
                    setRoute: viewModel.applyMixerControl,
                    setMeteringEnabled: viewModel.setMeteringEnabled)
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
