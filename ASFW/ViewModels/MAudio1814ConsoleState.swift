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
    /// Strips the front-panel assignable knob drives — the vendor's `ctrl`.
    ///
    /// The vendor keeps this as a 64-bit mask in the `MARotaryControlV2`
    /// property, one bit per channel *pair*, with a fixed base per group:
    /// SW return 0, input 16, output 32, aux output 50, headphone 56
    /// (`UserRotatedHardwareKnob` @ 0x21334). Sixteen of those bits are
    /// meaningful on an 1814. It is registered with a null setter, so the mask
    /// never reaches the device — it is host state, like mute and solo, and a
    /// set of strip ids is the same information in a form that cannot go out of
    /// step with the topology.
    private(set) var controlled: Set<String> = []
    private var intent: [UInt32: Int32] = [:]
    /// The console's host-only state is meaningful only for the topology from
    /// which its strip/control identities came.  Keep compatible state across
    /// ordinary meter/control refreshes, but remove identities that disappear
    /// when the driver commits a new geometry.
    private var lastTopologyRevision: UInt64?

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
    func displayLevel(_ control: UInt32, confirmed: Int32,
                      suppressed: Bool) -> Int32 {
        guard suppressed else { return confirmed }
        return intent[control] ?? confirmed
    }

    mutating func setIntendedLevel(_ control: UInt32, _ value: Int32) {
        intent[control] = value
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

    func isControlled(_ stripID: String) -> Bool { controlled.contains(stripID) }

    mutating func toggleControl(_ stripID: String) {
        if controlled.contains(stripID) {
            controlled.remove(stripID)
        } else {
            controlled.insert(stripID)
        }
    }

    /// Applies one movement of the assignable knob to every assigned strip.
    ///
    /// `delta` is in raw level units, which is the same scale the driver
    /// integrates the encoder in — one detent is 0x400, and levels are 0x100 per
    /// decibel, so a detent is 4 dB. The ALSA runtime scales its delta by
    /// `(vol_max - vol_min) / (rotary_max - rotary_min)`, which for this device
    /// is 1, so adding it directly matches the reference.
    ///
    /// Returns the writes to issue. A suppressed strip still moves — the knob
    /// sets what the fader will come back to — but nothing is written for it,
    /// because the device is deliberately holding silence there.
    mutating func applyLevelControllerDelta(
        _ delta: Int32, to topology: AudioTopologySnapshot
    ) -> [(UInt32, Int32)] {
        guard delta != 0 else { return [] }
        var writes: [(UInt32, Int32)] = []
        for strip in topology.strips where controlled.contains(strip.id) {
            let suppressed = isSuppressed(strip.id, kind: strip.kind)
            for channel in strip.channels {
                let current = intent[channel.levelControl] ?? channel.levelRaw
                let next = max(MAudio1814Level.rawMinimum,
                               min(MAudio1814Level.rawMaximum, current + delta))
                guard next != current else { continue }
                intent[channel.levelControl] = next
                if !suppressed {
                    writes.append((channel.levelControl, next))
                }
            }
        }
        return writes
    }

    /// Tracks the confirmed level of every unsuppressed channel, so a later mute
    /// has the right thing to restore — including a level a front-panel knob set
    /// rather than us. A suppressed channel is skipped: its confirmed value is
    /// the silence we wrote, and copying that in would make unmute a no-op.
    mutating func reconcile(with topology: AudioTopologySnapshot) {
        if lastTopologyRevision != topology.topologyRevision {
            lastTopologyRevision = topology.topologyRevision
            let stripIDs = Set(topology.strips.map(\.id))
            let controlIDs = Set(topology.strips.flatMap(\.channels)
                .map(\.levelControl))
            unlinked.formIntersection(stripIDs)
            muted.formIntersection(stripIDs)
            soloed.formIntersection(stripIDs)
            controlled.formIntersection(stripIDs)
            intent = intent.filter { controlIDs.contains($0.key) }
        }
        for strip in topology.strips where !isSuppressed(strip.id, kind: strip.kind) {
            for channel in strip.channels {
                intent[channel.levelControl] = channel.levelRaw
            }
        }
    }

    /// Every level write the device needs to match the current state. Only
    /// controls whose confirmed value already differs are returned, so a
    /// no-op toggle costs no bus traffic.
    func pendingLevelWrites(for topology: AudioTopologySnapshot) -> [(UInt32, Int32)] {
        var writes: [(UInt32, Int32)] = []
        for strip in topology.strips {
            let suppressed = isSuppressed(strip.id, kind: strip.kind)
            for channel in strip.channels {
                let target = suppressed
                    ? MAudio1814Level.rawMinimum
                    : (intent[channel.levelControl] ?? channel.levelRaw)
                if target != channel.levelRaw {
                    writes.append((channel.levelControl, target))
                }
            }
        }
        return writes
    }
}
