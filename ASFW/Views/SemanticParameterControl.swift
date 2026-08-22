import SwiftUI

/// A presentation selected from the driver-declared parameter domain. This
/// component intentionally knows no device control IDs or vendor command form.
struct SemanticParameterControl: View {
    let parameter: AudioSemanticTopologySnapshot.Parameter
    let value: Int32
    let title: String
    let isWriting: Bool
    let submit: (Int32) -> Void

    @State private var draft: Double

    init(
        parameter: AudioSemanticTopologySnapshot.Parameter,
        value: Int32,
        title: String,
        isWriting: Bool,
        submit: @escaping (Int32) -> Void
    ) {
        self.parameter = parameter
        self.value = value
        self.title = title
        self.isWriting = isWriting
        self.submit = submit
        _draft = State(initialValue: Double(value))
    }

    var body: some View {
        switch parameter.presentation {
        case .toggle:
            Button(title, systemImage: value == 0 ? "circle" : "checkmark.circle.fill", action: toggle)
                .disabled(isWriting)
                .accessibilityValue(value == 0 ? "Off" : "On")
        case .fader:
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(title)
                    Spacer()
                    if isWriting {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        Text(value, format: .number.precision(.fractionLength(0)))
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                }
                Slider(
                    value: $draft,
                    in: Double(parameter.minimum)...Double(parameter.maximum),
                    step: Double(parameter.step),
                    onEditingChanged: commitFader
                )
                .disabled(isWriting)
                .accessibilityLabel(title)
            }
            .onChange(of: value) { _, newValue in draft = Double(newValue) }
        case .selector:
            HStack {
                Text(title)
                Spacer()
                Button("Decrease", systemImage: "minus", action: decrement)
                    .disabled(isWriting || value <= parameter.minimum)
                    .labelStyle(.iconOnly)
                Text(value, format: .number)
                    .monospacedDigit()
                    .frame(minWidth: 24)
                Button("Increase", systemImage: "plus", action: increment)
                    .disabled(isWriting || value >= parameter.maximum)
                    .labelStyle(.iconOnly)
            }
        }
    }

    private func toggle() {
        submit(value == 0 ? parameter.maximum : parameter.minimum)
    }

    private func commitFader(_ editing: Bool) {
        guard !editing else { return }
        submit(Int32(draft.rounded()))
    }

    private func decrement() {
        submit(max(parameter.minimum, value - parameter.step))
    }

    private func increment() {
        submit(min(parameter.maximum, value + parameter.step))
    }
}
