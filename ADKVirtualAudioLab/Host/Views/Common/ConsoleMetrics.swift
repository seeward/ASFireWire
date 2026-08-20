import SwiftUI

/// Layout tokens for the hardware console.
///
/// Every control in a strip is a fixed size (fader, meter, knob), so row heights
/// are constants rather than measured values. That is what lets faders share a
/// baseline across strips of different content without a two-pass layout.
enum ConsoleMetrics {
    // 4pt spacing scale
    static let s1: CGFloat = 4
    static let s2: CGFloat = 8
    static let s3: CGFloat = 12
    static let s4: CGFloat = 16

    // Corner radii
    static let rControl: CGFloat = 4
    static let rStrip: CGFloat = 8
    static let rCard: CGFloat = 12

    // Control widths
    static let faderWidth: CGFloat = 48
    static let meterWidth: CGFloat = 22
    static let meterBarsWidth: CGFloat = 9
    static let knobWidth: CGFloat = 52

    // Strip geometry: one width for every strip, channel or master.
    static let stripContentWidth = faderWidth + s1 + meterWidth
    static let stripWidth = stripContentWidth + s2 * 2

    // Row heights
    static let rowBadge: CGFloat = 22
    static let rowSelector: CGFloat = 20
    static let rowPreamp: CGFloat = 20
    static let rowKnob: CGFloat = 64
    static let rowToggles: CGFloat = 22
    static let rowSendEnables: CGFloat = 20
    static let rowFader: CGFloat = 180
    static let rowReadout: CGFloat = 16

    // Surfaces
    static let stripFill = Color(white: 0.11)
    static let stripStroke = Color.white.opacity(0.08)
    static let masterStroke = Color.orange.opacity(0.30)
}

/// Which rows the whole console bank reserves.
///
/// A row is rendered by *every* strip if *any* strip in the bank needs it, so
/// faders line up; a row no strip needs is dropped entirely, so a small device
/// stays compact.
struct ConsoleRowPlan {
    var hasSelector = false
    var hasPreamp = false
    var auxCount = 0
    var hasPan = false
    var hasToggles = false
    var sendEnableCount = 0

    var auxHeight: CGFloat {
        guard auxCount > 0 else { return 0 }
        return CGFloat(auxCount) * ConsoleMetrics.rowKnob
            + CGFloat(auxCount - 1) * ConsoleMetrics.s2
    }

    static func plan(
        channels: [ChannelStripModel],
        masters: [OutputMasterStripModel]
    ) -> ConsoleRowPlan {
        var plan = ConsoleRowPlan()

        for channel in channels {
            if channel.nominalLevel != nil { plan.hasSelector = true }
            if channel.phantom != nil || channel.phase != nil { plan.hasPreamp = true }
            if channel.pan != nil { plan.hasPan = true }
            if channel.mute != nil || channel.solo != nil { plan.hasToggles = true }
            plan.auxCount = max(plan.auxCount, channel.auxSends.count)
            plan.sendEnableCount = max(plan.sendEnableCount, channel.sendEnables.count)
        }

        for master in masters where master.mute != nil || master.dim != nil {
            plan.hasToggles = true
        }

        return plan
    }
}
