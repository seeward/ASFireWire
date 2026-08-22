import SwiftUI

struct SemanticParameterSection: View {
    let title: String
    let parameters: [AudioSemanticTopologySnapshot.Parameter]
    let controls: AudioControlSurfaceSnapshot
    let parameterTitle: (AudioSemanticTopologySnapshot.Parameter) -> String
    let isWriting: (UInt32) -> Bool
    let submit: (AudioSemanticTopologySnapshot.Parameter, Int32) -> Void

    var body: some View {
        if !parameters.isEmpty {
            Section(title) {
                ForEach(parameters) { parameter in
                    SemanticParameterControl(
                        parameter: parameter,
                        value: controls.value(for: parameter.id),
                        title: parameterTitle(parameter),
                        isWriting: isWriting(parameter.id),
                        submit: { value in submit(parameter, value) }
                    )
                }
            }
        }
    }
}
