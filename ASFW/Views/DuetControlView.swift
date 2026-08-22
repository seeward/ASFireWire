import SwiftUI

struct DuetControlView: View {
    @StateObject private var viewModel: DuetConfigurationViewModel

    init(connector: ASFWDriverConnector) {
        _viewModel = StateObject(wrappedValue: DuetConfigurationViewModel(connector: connector))
    }

    var body: some View {
        Group {
            if let topology = viewModel.topology, let controls = viewModel.controls {
                List {
                    Section("Signal graph") {
                        LabeledContent("Nodes", value: topology.nodes.count.formatted())
                        LabeledContent("Fixed links", value: topology.fixedLinks.count.formatted())
                        LabeledContent("Routes", value: topology.routes.count.formatted())
                        LabeledContent("Mixer crosspoints", value: topology.crosspoints.count.formatted())
                    }

                    SemanticParameterSection(
                        title: "Inputs", parameters: viewModel.inputParameters, controls: controls,
                        parameterTitle: viewModel.title, isWriting: viewModel.writingParameterIDs.contains,
                        submit: viewModel.submit
                    )
                    SemanticParameterSection(
                        title: "Output", parameters: viewModel.outputParameters, controls: controls,
                        parameterTitle: viewModel.title, isWriting: viewModel.writingParameterIDs.contains,
                        submit: viewModel.submit
                    )
                    SemanticParameterSection(
                        title: "4 × 2 mixer", parameters: viewModel.mixerParameters, controls: controls,
                        parameterTitle: viewModel.title, isWriting: viewModel.writingParameterIDs.contains,
                        submit: viewModel.submit
                    )
                    SemanticParameterSection(
                        title: "Other controls", parameters: viewModel.otherParameters, controls: controls,
                        parameterTitle: viewModel.title, isWriting: viewModel.writingParameterIDs.contains,
                        submit: viewModel.submit
                    )
                }
            } else {
                ContentUnavailableView(
                    "No driver-owned Duet configuration available",
                    systemImage: "slider.horizontal.3",
                    description: Text(viewModel.statusText)
                )
            }
        }
        .navigationTitle("Duet")
        .safeAreaInset(edge: .bottom) {
            Text(viewModel.statusText)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal)
                .padding(.vertical, 8)
                .background(.bar)
        }
        .task { await viewModel.poll() }
    }

}
