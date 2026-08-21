import SwiftUI

struct AudioTopologyMeter: View {
    let pair: Int?
    let snapshot: AudioMeterSnapshot?
    let name: String

    var body: some View {
        let values = meterValues
        GeometryReader { geometry in
            HStack(spacing: 2) {
                ForEach(values.indices, id: \.self) { index in
                    ZStack(alignment: .bottom) {
                        RoundedRectangle(cornerRadius: 2).fill(Color(white: 0.05))
                        RoundedRectangle(cornerRadius: 2)
                            .fill(LinearGradient(colors: [.green, .yellow, .red], startPoint: .bottom, endPoint: .top))
                            .frame(height: max(3, geometry.size.height * CGFloat((values[index] + 128) / 128)))
                    }
                }
            }
        }
        .frame(width: 20)
        .accessibilityLabel("\(name) peak meter")
        .accessibilityValue(snapshot == nil ? "Unavailable" : "Live")
    }

    private var meterValues: [Double] {
        guard let pair else { return [-128, -128] }
        let left = snapshot?.decibels(at: pair * 2) ?? -128
        let right = snapshot?.decibels(at: pair * 2 + 1) ?? -128
        return [left, right]
    }
}
