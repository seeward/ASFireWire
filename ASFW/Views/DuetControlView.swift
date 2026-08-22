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
                    isWriting: viewModel.writingParameterIDs.contains,
                    submit: { control, value in viewModel.submit(control.parameter, value: value) }
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
