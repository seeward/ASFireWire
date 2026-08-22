import SwiftUI

struct DuetNominalLevelMenu: View {
    let inputName: String
    let control: DuetConsoleSnapshot.Control
    let isWriting: Bool
    let submit: (Int32) -> Void

    var body: some View {
        Menu {
            ForEach(Array(control.parameter.minimum...control.parameter.maximum), id: \.self) { value in
                Button(title(for: value)) { submit(value) }
            }
        } label: {
            HStack(spacing: 4) {
                Text(title(for: control.value))
                    .lineLimit(1)
                Spacer(minLength: 0)
                Image(systemName: "chevron.down")
                    .imageScale(.small)
            }
            .font(.caption.weight(.semibold))
            .padding(.horizontal, 7)
            .frame(maxWidth: .infinity, minHeight: 28)
            .background(Color.secondary.opacity(0.16), in: RoundedRectangle(cornerRadius: 6, style: .continuous))
        }
        .menuStyle(.borderlessButton)
        .disabled(isWriting)
        .accessibilityLabel("\(inputName) input mode")
        .accessibilityValue(title(for: control.value))
    }

    private func title(for value: Int32) -> String {
        switch value {
        case 0: "Mic"
        case 1: "+4 dBu"
        case 2: "−10 dBV"
        default: "Mode \(value)"
        }
    }
}
