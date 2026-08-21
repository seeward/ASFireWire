import Foundation
import Testing
@testable import ASFW

/// The app and the driver each carry their own copy of the 1814's control-group
/// table — `MAudio1814ControlGroup` here, `kMAudio1814ControlGroups` in
/// `MAudioSpecialParameters.hpp`. Nothing at build time ties them together, so
/// these tests pin the shape both sides have to agree on: the id encoding, the
/// group sizes, and the total. A driver-side group that grows without the app
/// following turns into controls that silently never render.
struct MAudio1814ControlIDTests {
    @Test func idEncodesGroupAndIndexInOneWord() {
        let control = MAudio1814ControlID(.headphoneVolume, 3)
        #expect(control.rawValue == (0x07 << 8) | 3)
        #expect(MAudio1814ControlID(rawValue: control.rawValue) == control)
    }

    @Test func rejectsAnIndexOutsideItsGroup() {
        #expect(MAudio1814ControlID(rawValue: (0x07 << 8) | 4) == nil)  // 4 headphones
        #expect(MAudio1814ControlID(rawValue: (0x0F << 8) | 1) == nil)  // 1 mask
        #expect(MAudio1814ControlID(rawValue: 0xDEAD_00) == nil)        // no such group
    }

    @Test func everyControlRoundTripsThroughItsRawValue() {
        let all = MAudio1814ControlID.all
        #expect(Set(all.map(\.rawValue)).count == all.count)
        for control in all {
            #expect(MAudio1814ControlID(rawValue: control.rawValue) == control)
        }
    }

    /// 78 = the eighteen ranges of the 160-byte parameter window. Mirrors
    /// `kMAudio1814ControlCount` and the C++ `ControlIdsAreUnique` test.
    @Test func exposesTheWholeParameterWindow() {
        #expect(MAudio1814ControlID.all.count == 78)
        #expect(MAudio1814ControlGroup.allCases.count == 18)
    }

    @Test(arguments: [
        (MAudio1814ControlGroup.mixerStreamGain, 4),
        (.analogOutputVolume, 4),
        (.mixerAnalogGain, 8),
        (.mixerSpdifGain, 2),
        (.mixerAdatGain, 8),
        (.auxOutputVolume, 2),
        (.headphoneVolume, 4),
        (.mixerAnalogBalance, 8),
        (.mixerSpdifBalance, 2),
        (.mixerAdatBalance, 8),
        (.auxStreamGain, 4),
        (.auxAnalogGain, 8),
        (.auxSpdifGain, 2),
        (.auxAdatGain, 8),
        (.physicalMixerSendMask, 1),
        (.streamMixerSendMask, 1),
        (.headphoneSource, 2),
        (.analogOutputSource, 2),
    ])
    func groupSizesMatchTheDeviceRegisterMap(group: MAudio1814ControlGroup, count: Int) {
        #expect(group.count == count)
    }

    /// Levels are attenuation with unity at zero; balance is centred and signed.
    /// Getting these backwards would write a full-scale value where the device
    /// expects a cut.
    @Test func valueRangesFollowTheDeviceConventions() {
        #expect(MAudio1814ControlID(.headphoneVolume, 0).valueRange == -32768...0)
        #expect(MAudio1814ControlID(.mixerAnalogBalance, 0).valueRange == -32768...32767)
        #expect(MAudio1814ControlID(.headphoneSource, 0).valueRange == 0...2)
        #expect(MAudio1814ControlID(.analogOutputSource, 0).valueRange == 0...1)
        #expect(MAudio1814ControlID(.physicalMixerSendMask).valueRange == 0...0x0003_FFFF)
        #expect(MAudio1814ControlID(.streamMixerSendMask).valueRange == 0...0x0F)
    }

    @Test func labelsIndexMultiControlGroupsAndLeaveMasksBare() {
        #expect(MAudio1814ControlID(.headphoneVolume, 2).label == "Headphone 3")
        #expect(MAudio1814ControlID(.physicalMixerSendMask).label == "Physical mixer sends")
    }

    /// Level and pan encoding live in `MAudio1814Level` and are covered by
    /// `MAudio1814LevelTests`; what belongs here is that the control *domains*
    /// agree with it.
    @Test func controlDomainsMatchTheLevelScale() {
        #expect(MAudio1814ControlID.levelMin == MAudio1814Level.rawMinimum)
        #expect(MAudio1814ControlID.levelMax == MAudio1814Level.rawMaximum)
        #expect(MAudio1814ControlID(.mixerAnalogBalance, 0).valueRange
            .contains(MAudio1814Level.panExtent))
    }
}
