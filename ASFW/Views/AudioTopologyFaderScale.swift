import SwiftUI

/// The dB scale printed between a 1814 strip's two faders, mirroring the
/// hardware's 0/2/5/10/20/30/50 marks.
struct AudioTopologyFaderScale: View {
    var body: some View {
        GeometryReader { geometry in
            let travel = max(10, geometry.size.height - 20)
            ZStack(alignment: .top) {
                ForEach(MAudio1814Level.faderTicksDb, id: \.self) { db in
                    let position = MAudio1814Level.position(
                        raw: MAudio1814Level.raw(decibels: db))
                    Text(db == 0 ? "0" : String(format: "%.0f", -db))
                        .font(.system(size: 7).monospaced())
                        .foregroundStyle(.secondary)
                        .offset(y: 10 + (1 - position) * travel - 4)
                }
            }
            .frame(maxWidth: .infinity, alignment: .center)
        }
        .frame(width: 20)
        .accessibilityHidden(true)
    }
}
