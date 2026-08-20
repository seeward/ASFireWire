import SwiftUI

/// Ordered history of what the controls did to the model, including refusals.
/// Reads the model's own log rather than re-deriving anything view-side, so a
/// cascade shows the same before/after the runtime recorded.
struct BehaviourLogSection: View {
    let events: [LabEventModel]
    @ObservedObject var state: VirtualLabState

    var body: some View {
        StudioCard(
            title: "Behaviour Log",
            systemImage: "list.bullet.rectangle",
            badge: "\(events.count) events"
        ) {
            VStack(alignment: .leading, spacing: ConsoleMetrics.s2) {
                HStack {
                    Text("Newest first · configuration changes show the resolved shape on both sides")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Clear", action: state.clearEventLog)
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .disabled(events.isEmpty)
                }

                if events.isEmpty {
                    Text("Move a control to record the first event.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.vertical, ConsoleMetrics.s3)
                } else {
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 0) {
                            ForEach(events) { event in
                                BehaviourLogRow(event: event)
                            }
                        }
                    }
                    .frame(maxHeight: 260)
                }
            }
        }
    }
}

private struct BehaviourLogRow: View {
    let event: LabEventModel

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: ConsoleMetrics.s2) {
            Text("#\(event.id)")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 40, alignment: .trailing)

            Text(event.kindText)
                .font(.system(size: 9, weight: .bold, design: .monospaced))
                .foregroundStyle(accent)
                .frame(width: 52, alignment: .leading)

            Text("rev \(event.revision)")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 48, alignment: .leading)

            VStack(alignment: .leading, spacing: 2) {
                Text(event.label)
                    .font(.system(size: 11, weight: .semibold))
                if !event.before.isEmpty || !event.after.isEmpty {
                    HStack(spacing: ConsoleMetrics.s1) {
                        Text(event.before)
                        Image(systemName: "arrow.right")
                            .font(.system(size: 8))
                        Text(event.after)
                    }
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundStyle(event.accepted ? .primary : .secondary)
                }
                if !event.detail.isEmpty {
                    Text(event.detail)
                        .font(.system(size: 10))
                        .foregroundStyle(event.accepted ? AnyShapeStyle(.secondary) : AnyShapeStyle(Color.red))
                }
            }

            Spacer(minLength: 0)
        }
        .padding(.vertical, ConsoleMetrics.s1)
        .padding(.horizontal, ConsoleMetrics.s2)
        .background(event.accepted ? Color.clear : Color.red.opacity(0.08))
        .accessibilityElement(children: .combine)
    }

    private var accent: Color {
        if !event.accepted { return .red }
        return event.isConfiguration ? .orange : .cyan
    }
}
