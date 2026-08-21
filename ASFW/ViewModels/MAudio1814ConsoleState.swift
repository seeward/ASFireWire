import Foundation

/// Host-side console state: link, mute and solo.
///
/// None of these is a device register. The 1814's parameter window has gains,
/// balances and routing and nothing else — the vendor driver computes the same
/// three from host state and folds them into the level it writes
/// (`AdjustedLevelForChannel` @ 0x1ee66 returns the group minimum when a channel
/// is muted or when solo is active elsewhere). We do the same.
///
/// Because the device only ever sees the *resulting* level, a muted channel's
/// intended level has to be remembered here or unmuting would have nothing to
/// restore. `intent` is that memory, seeded lazily from the confirmed snapshot
/// so an untouched control still reads back from the driver's belief.
///
/// The consequence worth knowing: this state lives only as long as the app does.
/// A mute does not survive a relaunch — the device stays at whatever level was
/// last written, which is silence. The vendor has the same property; its state
/// lived in the kext.
struct MAudio1814ConsoleState: Equatable {
    /// Strips whose two channels move together. Ganged by default, which is how
    /// the vendor ships and what people expect from a stereo pair.
    private(set) var unlinked: Set<String> = []
    private(set) var muted: Set<String> = []
    private(set) var soloed: Set<String> = []
    private var intent: [UInt32: Int32] = [:]

    var isSoloActive: Bool { !soloed.isEmpty }

    func isLinked(_ stripID: String) -> Bool { !unlinked.contains(stripID) }
    func isMuted(_ stripID: String) -> Bool { muted.contains(stripID) }
    func isSoloed(_ stripID: String) -> Bool { soloed.contains(stripID) }

    /// True when the strip should be writing silence regardless of its fader.
    ///
    /// Solo only suppresses other *input* strips. Soloing an input while the
    /// output masters fall silent would be useless, and the vendor scopes it the
    /// same way — solo appears on input strips only.
    func isSuppressed(_ stripID: String, kind: AudioTopologyStripKind) -> Bool {
        if muted.contains(stripID) { return true }
        guard kind.isInput, isSoloActive else { return false }
        return !soloed.contains(stripID)
    }

    /// What a fader should show.
    ///
    /// The device is authoritative whenever it is allowed to be: a front-panel
    /// knob moves a headphone level behind our back, and a fader that kept
    /// showing a remembered value would silently stop tracking the hardware.
    /// Only a suppressed strip falls back to the remembered level, because there
    /// the device is deliberately holding silence that is not the user's intent.
    func displayLevel(_ control: MAudio1814ControlID, confirmed: Int32,
                      suppressed: Bool) -> Int32 {
        guard suppressed else { return confirmed }
        return intent[control.rawValue] ?? confirmed
    }

    mutating func setIntendedLevel(_ control: MAudio1814ControlID, _ value: Int32) {
        intent[control.rawValue] = value
    }

    mutating func toggleLink(_ stripID: String) {
        if unlinked.contains(stripID) { unlinked.remove(stripID) } else { unlinked.insert(stripID) }
    }

    mutating func toggleMute(_ stripID: String) {
        if muted.contains(stripID) { muted.remove(stripID) } else { muted.insert(stripID) }
    }

    mutating func toggleSolo(_ stripID: String) {
        if soloed.contains(stripID) { soloed.remove(stripID) } else { soloed.insert(stripID) }
    }

    /// Tracks the confirmed level of every unsuppressed channel, so a later mute
    /// has the right thing to restore — including a level a front-panel knob set
    /// rather than us. A suppressed channel is skipped: its confirmed value is
    /// the silence we wrote, and copying that in would make unmute a no-op.
    mutating func reconcile(with topology: AudioTopologySnapshot) {
        for strip in topology.strips where !isSuppressed(strip.id, kind: strip.kind) {
            for channel in strip.channels {
                intent[channel.levelControl.rawValue] = channel.levelRaw
            }
        }
    }

    /// Every level write the device needs to match the current state. Only
    /// controls whose confirmed value already differs are returned, so a
    /// no-op toggle costs no bus traffic.
    func pendingLevelWrites(for topology: AudioTopologySnapshot) -> [(MAudio1814ControlID, Int32)] {
        var writes: [(MAudio1814ControlID, Int32)] = []
        for strip in topology.strips {
            let suppressed = isSuppressed(strip.id, kind: strip.kind)
            for channel in strip.channels {
                let target = suppressed
                    ? MAudio1814Level.rawMinimum
                    : (intent[channel.levelControl.rawValue] ?? channel.levelRaw)
                if target != channel.levelRaw {
                    writes.append((channel.levelControl, target))
                }
            }
        }
        return writes
    }
}
