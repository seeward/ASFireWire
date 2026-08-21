import Foundation
import Testing
@testable import ASFW

/// Mute, solo and link are host-side arithmetic on the level registers — the
/// device has no mute bit. The risk that carries is losing the user's fader
/// position when we write silence over it, so these tests are mostly about the
/// restore path.
struct MAudio1814ConsoleStateTests {
    private func strip(_ id: String, kind: AudioTopologyStripKind,
                       group: MAudio1814ControlGroup, pair: UInt32,
                       level: Int32) -> AudioTopologyStrip {
        AudioTopologyStrip(
            id: id, name: id, kind: kind,
            channels: (0..<2).map { side in
                AudioTopologyStripChannel(
                    id: "\(id)-\(side)", label: side == 0 ? "L" : "R",
                    levelControl: MAudio1814ControlID(group, pair * 2 + UInt32(side)),
                    levelRaw: level,
                    panControl: nil, panRaw: 0,
                    auxControl: nil, auxRaw: 0,
                    meterIndex: nil)
            },
            sends: [], source: nil)
    }

    private func topology(_ strips: [AudioTopologyStrip]) -> AudioTopologySnapshot {
        AudioTopologySnapshot(revision: 1, strips: strips, routes: [])
    }

    @Test func stripsAreLinkedByDefault() {
        let state = MAudio1814ConsoleState()
        #expect(state.isLinked("analog-0"))
        #expect(!state.isMuted("analog-0"))
        #expect(!state.isSoloActive)
    }

    @Test func muteWritesSilenceAndUnmuteRestoresTheFader() {
        let unity: Int32 = 0
        let model = topology([strip("analog-0", kind: .physicalInput,
                                    group: .mixerAnalogGain, pair: 0, level: unity)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)

        state.toggleMute("analog-0")
        let muteWrites = state.pendingLevelWrites(for: model)
        #expect(muteWrites.count == 2)
        #expect(muteWrites.allSatisfy { $0.1 == MAudio1814Level.rawMinimum })

        // The device now holds silence; that is what the next snapshot reports.
        let silenced = topology([strip("analog-0", kind: .physicalInput,
                                       group: .mixerAnalogGain, pair: 0,
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
                                    group: .mixerAnalogGain, pair: 0, level: -2560)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        state.toggleMute("analog-0")

        let silenced = topology([strip("analog-0", kind: .physicalInput,
                                       group: .mixerAnalogGain, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.reconcile(with: silenced)
        state.toggleMute("analog-0")

        #expect(state.pendingLevelWrites(for: silenced).allSatisfy { $0.1 == -2560 })
    }

    @Test func soloSuppressesOtherInputsAndLeavesOutputsAlone() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, group: .mixerAnalogGain, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, group: .mixerAnalogGain, pair: 1, level: 0),
            strip("out", kind: .output, group: .analogOutputVolume, pair: 0, level: 0),
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
        #expect(writes.allSatisfy { $0.0.group == .mixerAnalogGain && $0.0.index >= 2 })
    }

    @Test func clearingTheLastSoloReleasesEverything() {
        let model = topology([
            strip("analog-0", kind: .physicalInput, group: .mixerAnalogGain, pair: 0, level: 0),
            strip("analog-1", kind: .physicalInput, group: .mixerAnalogGain, pair: 1, level: 0),
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
                                    group: .mixerAnalogGain, pair: 0, level: 0)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: model)
        #expect(state.pendingLevelWrites(for: model).isEmpty)
    }

    /// The regression that shipped: an unsuppressed fader must follow the
    /// device. The 1814's front-panel knobs write headphone levels behind our
    /// back, so a strip that kept showing a remembered value would quietly stop
    /// tracking the hardware.
    @Test func unsuppressedFadersFollowTheDeviceNotTheRememberedLevel() {
        let control = MAudio1814ControlID(.headphoneVolume, 0)
        var state = MAudio1814ConsoleState()
        state.reconcile(with: topology([strip("phones-0", kind: .headphone,
                                              group: .headphoneVolume, pair: 0, level: 0)]))

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
                                      group: .headphoneVolume, pair: 0, level: -2048)])
        var state = MAudio1814ConsoleState()
        state.reconcile(with: topology([strip("phones-0", kind: .headphone,
                                              group: .headphoneVolume, pair: 0, level: 0)]))
        state.reconcile(with: knobbed)
        state.toggleMute("phones-0")

        let silenced = topology([strip("phones-0", kind: .headphone,
                                       group: .headphoneVolume, pair: 0,
                                       level: MAudio1814Level.rawMinimum)])
        state.toggleMute("phones-0")
        #expect(state.pendingLevelWrites(for: silenced).allSatisfy { $0.1 == -2048 })
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
