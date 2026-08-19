import SwiftUI

struct NodeDrivenProcessorsSection: View {
    let procNodes: [NodeModel]
    let snap: LabDeviceSnapshot
    @ObservedObject var state: VirtualLabState

    var body: some View {
        StudioCard(title: "Hardware DSP Processors", systemImage: "cpu.fill", badge: "\(procNodes.count) Engines") {
            HStack(alignment: .top, spacing: 14) {
                ForEach(procNodes) { node in
                    let params = snap.parameters(forTargetNode: node.id)

                    VStack(alignment: .leading, spacing: 8) {
                        Text(node.name.uppercased())
                            .font(.caption.bold())
                            .foregroundStyle(.purple)

                        if !params.isEmpty {
                            VStack(alignment: .leading, spacing: 6) {
                                ForEach(params) { p in
                                    if p.kind == ASFW_PARAM_KIND_BOOLEAN {
                                        Toggle(p.name, isOn: Binding(
                                            get: { p.boolValue },
                                            set: { state.setParameterBool(id: p.id, value: $0) }
                                        ))
                                        .font(.caption)
                                    } else if p.kind == ASFW_PARAM_KIND_SCALAR {
                                        HStack {
                                            Text(p.name).font(.caption2).foregroundStyle(.secondary)
                                            Spacer()
                                            Text(String(format: "%.0f %@", p.scalarValue, p.unit)).font(.caption2.monospaced())
                                        }
                                        Slider(
                                            value: Binding(
                                                get: { p.scalarValue },
                                                set: { state.setParameterScalar(id: p.id, value: $0) }
                                            ),
                                            in: p.scalarMin...p.scalarMax,
                                            step: p.scalarStep
                                        )
                                    }
                                }
                            }
                        } else {
                            Text("Active DSP Engine (Fixed Transfer Function)")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .padding(10)
                    .frame(maxWidth: .infinity)
                    .background(Color.secondary.opacity(0.04))
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                }
            }
        }
    }
}
