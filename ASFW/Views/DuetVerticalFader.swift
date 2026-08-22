import SwiftUI

struct DuetVerticalFader: View {
    let title: String
    let control: DuetConsoleSnapshot.Control
    let isWriting: Bool
    let submit: (Int32) -> Void

    @State private var draft: Double
    @State private var dragStartValue: Double?

    init(
        title: String,
        control: DuetConsoleSnapshot.Control,
        isWriting: Bool,
        submit: @escaping (Int32) -> Void
    ) {
        self.title = title
        self.control = control
        self.isWriting = isWriting
        self.submit = submit
        _draft = State(initialValue: Double(control.value))
    }

    var body: some View {
        GeometryReader { proxy in
            let trackHeight = max(16, proxy.size.height - 24)
            let thumbOffset = (1 - normalizedValue) * trackHeight

            ZStack(alignment: .top) {
                DuetFaderScale(height: trackHeight)
                    .offset(y: 12)

                Capsule()
                    .fill(.black.opacity(0.32))
                    .frame(width: 7, height: trackHeight)
                    .overlay(Capsule().strokeBorder(.white.opacity(0.15)))
                    .offset(y: 12)

                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(.tertiary)
                    .frame(width: 34, height: 22)
                    .overlay(Rectangle().fill(Color.white.opacity(0.7)).frame(width: 22, height: 2))
                    .shadow(color: .black.opacity(0.3), radius: 2, y: 1)
                    .offset(y: thumbOffset + 1)
            }
            .contentShape(Rectangle())
            .gesture(dragGesture(trackHeight: trackHeight))
        }
        .frame(width: 46, height: 168)
        .opacity(isWriting ? 0.55 : 1)
        .onChange(of: control.value) { _, value in
            guard dragStartValue == nil else { return }
            draft = Double(value)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(title)
        .accessibilityValue(displayValue)
        .accessibilityAdjustableAction { direction in
            switch direction {
            case .increment: submit(quantized(draft + Double(step)))
            case .decrement: submit(quantized(draft - Double(step)))
            @unknown default: break
            }
        }
    }

    private var minimum: Double { Double(control.parameter.minimum) }
    private var maximum: Double { Double(control.parameter.maximum) }
    private var step: Int32 { max(1, control.parameter.step) }
    private var normalizedValue: Double {
        let span = maximum - minimum
        guard span > 0 else { return 0 }
        return min(1, max(0, (draft - minimum) / span))
    }
    private var displayValue: String {
        control.parameter.unit == .decibels ? "\(Int(draft.rounded())) dB" : "\(Int(draft.rounded()))"
    }

    private func dragGesture(trackHeight: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { gesture in
                guard !isWriting else { return }
                let start = dragStartValue ?? draft
                dragStartValue = start
                let span = maximum - minimum
                draft = min(maximum, max(minimum, start - Double(gesture.translation.height / trackHeight) * span))
            }
            .onEnded { _ in
                guard !isWriting else { return }
                let value = quantized(draft)
                draft = Double(value)
                dragStartValue = nil
                submit(value)
            }
    }

    private func quantized(_ value: Double) -> Int32 {
        let rounded = minimum + ((value - minimum) / Double(step)).rounded() * Double(step)
        return Int32(min(maximum, max(minimum, rounded)))
    }
}

private struct DuetFaderScale: View {
    let height: CGFloat

    var body: some View {
        HStack {
            tickColumn
            Spacer()
            tickColumn
        }
        .frame(width: 42, height: height)
    }

    private var tickColumn: some View {
        VStack {
            ForEach(0..<6) { index in
                Rectangle()
                    .fill(Color.secondary.opacity(index == 0 || index == 5 ? 0.8 : 0.35))
                    .frame(width: index == 0 || index == 5 ? 6 : 3, height: 1)
                if index < 5 { Spacer() }
            }
        }
    }
}
