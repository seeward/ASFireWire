import SwiftUI

/// One channel fader, positioned 0...1 by `MAudio1814Level` rather than by a
/// raw percentage — the register is linear in dB, so a linear-in-raw fader puts
/// the whole useful range in the top of the throw.
struct AudioTopologyFader: View {
    let position: Double
    let tint: Color
    var isEnabled = true
    let onChanged: (Double) -> Void

    @State private var dragStart: Double?
    /// The position being dragged right now. Confirmed state arrives from the
    /// driver on its own cadence, which is slower than a drag; rendering that
    /// directly makes the thumb stutter and snap backwards under the cursor.
    @State private var live: Double?

    var body: some View {
        GeometryReader { geometry in
            let travel = max(10, geometry.size.height - 20)
            let shown = live ?? position
            let thumbY = (1 - shown) * travel
            ZStack(alignment: .top) {
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color(white: 0.05))
                    .frame(width: 5, height: travel)
                    .offset(y: 10)
                RoundedRectangle(cornerRadius: 2)
                    .fill(tint.opacity(isEnabled ? 0.55 : 0.2))
                    .frame(width: 5, height: max(0, travel * shown))
                    .offset(y: 10 + thumbY)
                RoundedRectangle(cornerRadius: 3)
                    .fill(LinearGradient(colors: [Color(white: 0.75), Color(white: 0.95),
                                                  Color(white: 0.6)],
                                         startPoint: .top, endPoint: .bottom))
                    .frame(width: 26, height: 16)
                    .overlay { Rectangle().fill(tint.opacity(isEnabled ? 1 : 0.3)).frame(height: 2) }
                    .shadow(radius: 1, y: 1)
                    .offset(y: thumbY + 2)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { gesture in
                    guard isEnabled else { return }
                    let start = dragStart ?? shown
                    dragStart = start
                    let next = max(0, min(1, start - Double(gesture.translation.height) / travel))
                    live = next
                    onChanged(next)
                }
                .onEnded { _ in
                    dragStart = nil
                    live = nil
                })
        }
        .frame(width: 30)
        .opacity(isEnabled ? 1 : 0.45)
        .accessibilityAdjustableAction { direction in
            guard isEnabled else { return }
            onChanged(max(0, min(1, position + (direction == .increment ? 0.02 : -0.02))))
        }
    }
}

/// The dB scale printed between a strip's two faders, mirroring the vendor's
/// 0/2/5/10/20/30/50 marks.
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
