import SwiftUI

struct MixerCrosspointCell: View {
    let crosspoint: MixerCrosspointModel?
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        if let cp = crosspoint, let param = snap.parameter(forTargetCrosspoint: cp.id) {
            ZStack {
                RoundedRectangle(cornerRadius: 3)
                    .fill(param.scalarValue > param.scalarMin ? Color.accentColor.opacity(0.2) : Color.secondary.opacity(0.08))

                Text(param.scalarValue <= param.scalarMin ? "-∞" : String(format: "%.0f", param.scalarValue))
                    .font(.system(size: 9, weight: .bold, design: .monospaced))
                    .foregroundStyle(param.scalarValue > param.scalarMin ? Color.cyan : Color.secondary)
            }
            .frame(height: 20)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let delta = -gesture.translation.height * 0.5
                        let newVal = max(param.scalarMin, min(param.scalarMax, param.scalarValue + delta))
                        state.setParameterScalar(id: param.id, value: newVal)
                    }
            )
        } else {
            Rectangle()
                .fill(Color.clear)
                .frame(height: 20)
        }
    }
}
