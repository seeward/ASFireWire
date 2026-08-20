import SwiftUI

struct VerticalMasterStrip: View {
    let master: OutputMasterStripModel
    let plan: ConsoleRowPlan
    @ObservedObject var state: VirtualLabState

    var body: some View {
        VStack(spacing: ConsoleMetrics.s2) {
            nameBadge
                .frame(height: ConsoleMetrics.rowBadge)

            // Masters carry none of the channel controls, but they reserve the
            // rows so their faders land on the channel-fader baseline.
            if plan.hasSelector {
                Color.clear.frame(height: ConsoleMetrics.rowSelector)
            }
            if plan.hasPreamp {
                Color.clear.frame(height: ConsoleMetrics.rowPreamp)
            }
            if plan.auxCount > 0 {
                Color.clear.frame(height: plan.auxHeight)
            }
            if plan.hasPan {
                Color.clear.frame(height: ConsoleMetrics.rowKnob)
            }
            if plan.hasToggles {
                togglesRow.frame(height: ConsoleMetrics.rowToggles)
            }

            faderRow.frame(height: ConsoleMetrics.rowFader)
            readoutRow.frame(height: ConsoleMetrics.rowReadout)
        }
        .padding(ConsoleMetrics.s2)
        .frame(width: ConsoleMetrics.stripWidth)
        .background(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .fill(ConsoleMetrics.stripFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ConsoleMetrics.rStrip, style: .continuous)
                .strokeBorder(ConsoleMetrics.masterStroke, lineWidth: 1)
        )
    }

    // MARK: - Rows

    private var nameBadge: some View {
        Text(master.name.uppercased())
            .font(.system(size: 10, weight: .bold, design: .monospaced))
            .lineLimit(1)
            .minimumScaleFactor(0.8)
            .foregroundStyle(.orange)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color.orange.opacity(0.18))
            .clipShape(RoundedRectangle(cornerRadius: ConsoleMetrics.rControl))
    }

    @ViewBuilder
    private var togglesRow: some View {
        HStack(spacing: ConsoleMetrics.s1) {
            if let mute = master.mute {
                ConsoleToggle(
                    glyph: "MUTE",
                    label: "\(master.name) mute",
                    isOn: mute.boolValue,
                    tint: .red,
                    onForeground: .white,
                    fontSize: 9,
                    glowsWhenOn: true
                ) {
                    state.setParameterBool(id: mute.id, value: !mute.boolValue)
                }
            }
            if let dim = master.dim {
                ConsoleToggle(
                    glyph: "DIM",
                    label: "\(master.name) dim",
                    isOn: dim.boolValue,
                    tint: .yellow,
                    fontSize: 9,
                    glowsWhenOn: true
                ) {
                    state.setParameterBool(id: dim.id, value: !dim.boolValue)
                }
            }
            if master.mute == nil && master.dim == nil {
                Color.clear
            }
        }
    }

    private var faderRow: some View {
        HStack(spacing: ConsoleMetrics.s1) {
            if let level = master.level {
                VerticalAudioFader(
                    value: level.scalarValue,
                    range: level.scalarMin...level.scalarMax,
                    step: level.scalarStep,
                    unit: level.unit,
                    onValueChange: { state.setParameterScalar(id: level.id, value: $0) }
                )
                .accessibilityLabel("\(master.name) level")
            } else {
                Color.clear.frame(width: ConsoleMetrics.faderWidth)
            }

            // A stereo master shows two bars inside one meter slot, so masters
            // stay the same width as channels.
            if let first = master.meters.first {
                VerticalAudioMeter(
                    values: master.meters.prefix(2).map(\.value),
                    range: first.min...first.max,
                    name: master.name
                )
            } else {
                Color.clear.frame(width: ConsoleMetrics.meterWidth)
            }
        }
    }

    @ViewBuilder
    private var readoutRow: some View {
        if let level = master.level {
            Text(level.scalarValue <= level.scalarMin ? "-∞" : String(format: "%.1f dB", level.scalarValue))
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(.orange)
        } else {
            Color.clear
        }
    }
}
