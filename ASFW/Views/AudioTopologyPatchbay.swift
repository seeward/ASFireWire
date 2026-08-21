import SwiftUI

struct AudioTopologyPatchbay: View {
    let topology: AudioTopologySnapshot
    let setRoute: (MAudio1814ControlID, Int32) -> Void

    var body: some View {
        AudioTopologyCard(title: "Signal Routing Patchbay", systemImage: "point.3.filled.connected.trianglepath.dotted", badge: "\(topology.routes.count) Routers") {
            VStack(alignment: .leading, spacing: 10) {
                ForEach(topology.routes) { route in
                    HStack(spacing: 16) {
                        Text(route.name).font(.subheadline.bold()).frame(width: 210, alignment: .leading)
                        HStack(spacing: 8) {
                            ForEach(route.choices) { choice in
                                Button(choice.name) { setRoute(route.control, choice.value) }
                                    .buttonStyle(.plain)
                                    .font(.caption.bold())
                                    .padding(.horizontal, 12).padding(.vertical, 8)
                                    .background(route.selectedValue == choice.value ? Color.accentColor : Color.secondary.opacity(0.14))
                                    .foregroundStyle(route.selectedValue == choice.value ? Color.white : Color.primary)
                                    .clipShape(RoundedRectangle(cornerRadius: 6))
                                    .accessibilityAddTraits(route.selectedValue == choice.value ? .isSelected : [])
                            }
                        }
                    }
                }
            }
        }
    }
}
