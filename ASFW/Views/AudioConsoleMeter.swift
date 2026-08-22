import SwiftUI

/// Device-neutral peak meter. Families supply normalized levels and preserve
/// their own meter scaling/ballistics in their model layer.
struct AudioConsoleMeter: View {
    let level: Double
    let heldPeak: Double?
    let label: String
    let valueDescription: String
    var width: CGFloat = 13
    var height: CGFloat? = nil

    var body: some View {
        GeometryReader { geometry in
            let fill = min(1, max(0, level))
            let peak = heldPeak.map { min(1, max(0, $0)) }

            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color(white: 0.05))

                RoundedRectangle(cornerRadius: 2)
                    .fill(levelGradient)
                    .frame(height: max(0, geometry.size.height * fill))

                if let peak, peak > 0.002 {
                    Rectangle()
                        .fill(peak > 0.98 ? Color.red : Color.white)
                        .frame(height: 2)
                        .offset(y: -max(0, geometry.size.height * peak - 2))
                }
            }
        }
        .frame(width: width, height: height)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .accessibilityValue(valueDescription)
    }

    private var levelGradient: LinearGradient {
        LinearGradient(
            stops: [
                .init(color: .green, location: 0),
                .init(color: .green, location: 0.72),
                .init(color: .yellow, location: 0.88),
                .init(color: .red, location: 1),
            ],
            startPoint: .bottom,
            endPoint: .top
        )
    }
}
