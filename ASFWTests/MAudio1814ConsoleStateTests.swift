import Foundation
import Testing
@testable import ASFW

/// Mute, solo and link are host-side arithmetic on the level registers — the
/// device has no mute bit. The risk that carries is losing the user's fader
/// position when we write silence over it, so these tests are mostly about the
/// restore path.
struct MAudio1814ConsoleStateTests {
    private func strip(_ id: String, kind: AudioTopologyStripKind,
                       controlBase: UInt32, pair: UInt32,
                       level: Int32) -> AudioTopologyStrip {
        AudioTopologyStrip(
            id: id, name: id, kind: kind,
            channels: (0..<2).map { side in
                AudioTopologyStripChannel(
                    id: "\(id)-\(side)", label: side == 0 ? "L" : "R",
                    levelControl: controlBase + pair * 2 + UInt32(side),
                    levelRaw: level,
                    panControl: nil, panRaw: 0,
                    auxControl: nil, auxRaw: 0,
                    meterIndex: nil)
            },
            sends: [], source: nil)
    }

    private func topology(_ strips: [AudioTopologyStrip], revision: UInt64 = 1) -> AudioTopologySnapshot {
        AudioTopologySnapshot(topologyRevision: revision, strips: strips, routes: [])
    }

    @Test func stripsAreLinkedByDefault() {
        let state = MAudio1814ConsoleState()
        #expect(state.isLinked("analog-0"))
        #expect(!state.isMuted("analog-0"))
        #expect(!state.isSoloActive)
    }

    @Test func topologyChangePrunesHostOnlyStateForRemovedStrips() {
        var state = MAudio1814ConsoleState()
        let original = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, controlBase: 0x0300, pair: 1, level: 0),
        ])
        state.reconcile(with: original)
        state.toggleMute("analog-1")
        state.toggleSolo("analog-1")
        state.toggleControl("analog-1")
        state.setIntendedLevel(0x0302, -1024)

        let changed = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
        ], revision: 2)
        state.reconcile(with: changed)

        #expect(!state.isMuted("analog-1"))
        #expect(!state.isSoloed("analog-1"))
        #expect(!state.isControlled("analog-1"))
        #expect(!state.isSoloActive)
        #expect(state.pendingLevelWrites(for: changed).isEmpty)
    }

    @Test func muteWritesSilenceAndUnmuteRestoresTheFader() {
        let unity: Int32 = 0
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: unity)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)

        state.toggleMute("analog-0")
        let muteWrites = state.pendingLevelWrites(for: model)
        #expect(muteWrites.count == 2)
        #expect(muteWrites.allSatisfy { $0.1 == MAudio1814Level.rawMinimum })

        // The device now holds silence; that is what the next snapshot reports.
        let silenced = topology([strip("analog-0", kind: .physicalInput,
                                       controlBase: 0x0300, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.toggleMute("analog-0")
        let restoreWrites = state.pendingLevelWrites(for: silenced)
        #expect(restoreWrites.count == 2)
        #expect(restoreWrites.allSatisfy { $0.1 == unity })
    }

    /// Reconciling must not overwrite the remembered level with the silence the
    /// device is currently holding, or unmute would restore nothing.
    @Test func reconcilingWhileMutedDoesNotForgetTheIntendedLevel() {
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: -2560)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleMute("analog-0")

        let silenced = topology([strip("analog-0", kind: .physicalInput,
                                       controlBase: 0x0300, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.reconcile(with: silenced)
        state.toggleMute("analog-0")

        #expect(state.pendingLevelWrites(for: silenced).allSatisfy { $0.1 == -2560 })
    }

    @Test func soloSuppressesOtherInputsAndLeavesOutputsAlone() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, controlBase: 0x0300, pair: 1, level: 0),
            strip("out", kind: .output, controlBase: 0x0200, pair: 0, level: 0),
        ])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleSolo("analog-0")

        #expect(state.isSoloActive)
        #expect(!state.isSuppressed("analog-0", kind: .physicalInput))
        #expect(state.isSuppressed("analog-1", kind: .physicalInput))
        // Silencing the outputs would make solo useless.
        #expect(!state.isSuppressed("out", kind: .output))

        let writes = state.pendingLevelWrites(for: model)
        #expect(writes.count == 2)
        #expect(writes.allSatisfy { $0.0 >= 0x0302 && $0.0 < 0x0308 })
    }

    @Test func clearingTheLastSoloReleasesEverything() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, controlBase: 0x0300, pair: 1, level: 0),
        ])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleSolo("analog-0")
        state.toggleSolo("analog-0")

        #expect(!state.isSoloActive)
        #expect(!state.isSuppressed("analog-1", kind: .physicalInput))
    }

    /// Mute outranks solo: soloing a muted strip must not un-mute it.
    @Test func muteWinsOverSolo() {
        var state = MAudio1814ConsoleState()
        state.toggleMute("analog-0")
        state.toggleSolo("analog-0")
        #expect(state.isSuppressed("analog-0", kind: .physicalInput))
    }

    @Test func producesNoWritesWhenTheDeviceAlreadyAgrees() {
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: 0)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        #expect(state.pendingLevelWrites(for: model).isEmpty)
    }

    /// The regression that shipped: an unsuppressed fader must follow the
    /// device. The 1814's front-panel knobs write headphone levels behind our
    /// back, so a strip that kept showing a remembered value would quietly stop
    /// tracking the hardware.
    @Test func unsuppressedFadersFollowTheDeviceNotTheRememberedLevel() {
        let control: UInt32 = 0x0700
        var state = MAudio1814ConsoleState()
        state.reconcile(with: topology([strip("phones-0", kind: .headphone,
                                              controlBase: 0x0700, pair: 0, level: 0)]))

        // A front-panel knob has since pulled it down by 8 dB.
        #expect(state.displayLevel(control, confirmed: -2048, suppressed: false) == -2048)
        // Muted, the remembered level shows instead of the silence on the device.
        state.toggleMute("phones-0")
        #expect(state.displayLevel(control, confirmed: MAudio1814Level.rawMinimum,
                                   suppressed: true) == 0)
    }

    /// ...and the remembered level tracks the knob too, so unmuting after a
    /// hardware change restores where the hardware was, not where we last were.
    @Test func remembersLevelsSetByTheHardwareNotJustByUs() {
        let knobbed = topology([strip("phones-0", kind: .headphone,
                                      controlBase: 0x0700, pair: 0, level: -2048)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: topology([strip("phones-0", kind: .headphone,
                                              controlBase: 0x0700, pair: 0, level: 0)]))
        state.reconcile(with: knobbed)
        state.toggleMute("phones-0")

        let silenced = topology([strip("phones-0", kind: .headphone,
                                       controlBase: 0x0700, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.toggleMute("phones-0")
        #expect(state.pendingLevelWrites(for: silenced).allSatisfy { $0.1 == -2048 })
    }

    // MARK: - Level controller (the vendor's `ctrl`)

    @Test func nothingIsAssignedToTheLevelControllerByDefault() {
        let state = MAudio1814ConsoleState()
        #expect(!state.isControlled("analog-0"))
        #expect(state.controlled.isEmpty)
    }

    /// One detent is 0x400 in the encoder's units and levels are 0x100 per
    /// decibel, so a detent is 4 dB and the raw delta applies directly. The ALSA
    /// runtime scales by (vol range / rotary range), which is 1 here.
    @Test func theKnobMovesOnlyAssignedStrips() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, controlBase: 0x0300, pair: 1, level: 0),
        ])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleControl("analog-0")

        let writes = state.applyLevelControllerDelta(-0x400, to: model)
        #expect(writes.count == 2)
        #expect(writes.allSatisfy { $0.1 == -0x400 })
        #expect(writes.allSatisfy { $0.0 >= 0x0300 && $0.0 < 0x0302 })
        // -0x400 raw is -4 dB.
        #expect(MAudio1814Level.decibels(raw: writes[0].1) == -4)
    }

    @Test func theKnobDrivesEveryAssignedStripTogether() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, controlBase: 0x0300, pair: 0, level: 0),
            strip("phones-0", kind: .headphone, controlBase: 0x0700, pair: 0, level: 0),
        ])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleControl("analog-0")
        state.toggleControl("phones-0")

        let writes = state.applyLevelControllerDelta(-0x400, to: model)
        #expect(writes.count == 4)
        #expect(Set(writes.map { $0.0 >> 8 }) == [0x03, 0x07])
    }

    @Test func theKnobClampsAtTheEndsOfTheRange() {
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: 0)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleControl("analog-0")

        // Already at unity: turning up produces nothing rather than overflowing.
        #expect(state.applyLevelControllerDelta(0x400, to: model).isEmpty)

        let floored = topology([strip("analog-0", kind: .physicalInput,
                                      controlBase: 0x0300, pair: 0,
                                      level: MAudio1814Level.rawMinimum)])
        var atFloor = MAudio1814ConsoleState()
        atFloor.reconcile(with: floored)
        atFloor.toggleControl("analog-0")
        #expect(atFloor.applyLevelControllerDelta(-0x400, to: floored).isEmpty)
    }

    /// A muted strip still follows the knob — the knob is setting what the fader
    /// comes back to — but nothing is written while the device holds silence.
    @Test func theKnobMovesAMutedStripWithoutWritingToIt() {
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: 0)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleControl("analog-0")
        state.toggleMute("analog-0")

        #expect(state.applyLevelControllerDelta(-0x400, to: model).isEmpty)

        let silenced = topology([strip("analog-0", kind: .physicalInput,
                                       controlBase: 0x0300, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.toggleMute("analog-0")
        #expect(state.pendingLevelWrites(for: silenced).allSatisfy { $0.1 == -0x400 })
    }

    @Test func aStationaryKnobWritesNothing() {
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    controlBase: 0x0300, pair: 0, level: 0)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleControl("analog-0")
        #expect(state.applyLevelControllerDelta(0, to: model).isEmpty)
    }

    @Test func linkTogglesIndependentlyPerStrip() {
        var state = MAudio1814ConsoleState()
        state.toggleLink("analog-0")
        #expect(!state.isLinked("analog-0"))
        #expect(state.isLinked("analog-1"))
        state.toggleLink("analog-0")
        #expect(state.isLinked("analog-0"))
    }
}
