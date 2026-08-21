import SwiftUI

struct AudioTopologyFader: View {
    let value: Double
    let onChanged: (Double) -> Void
    @State private var dragStartValue: Double?

    var body: some View {
        GeometryReader { geometry in
            let trackHeight = max(10, geometry.size.height - 26)
            let thumbY = (1 - value / 100) * trackHeight
            ZStack(alignment: .top) {
                RoundedRectangle(cornerRadius: 3).fill(Color(white: 0.05))
                    .frame(width: 6, height: trackHeight).offset(y: 13)
                RoundedRectangle(cornerRadius: 4)
                    .fill(LinearGradient(colors: [.gray, .white.opacity(0.7), .gray], startPoint: .top, endPoint: .bottom))
                    .frame(width: 34, height: 22)
                    .overlay { Rectangle().fill(Color.white).frame(width: 24, height: 2) }
                    .offset(y: thumbY + 2)
                    .gesture(DragGesture(minimumDistance: 0)
                        .onChanged { gesture in
                            let start = dragStartValue ?? value
                            dragStartValue = start
                            onChanged(max(0, min(100, start - Double(gesture.translation.height) / Double(trackHeight) * 100)))
                        }
                        .onEnded { _ in dragStartValue = nil })
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(width: 46)
        .accessibilityAdjustableAction { direction in
            onChanged(max(0, min(100, value + (direction == .increment ? 1 : -1))))
        }
    }
}
