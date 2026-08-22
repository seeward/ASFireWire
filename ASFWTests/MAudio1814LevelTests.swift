import Foundation
import Testing
@testable import ASFW

/// The 1814's level registers are linear in decibels, not amplitude. Getting
/// that backwards is invisible — nothing errors, the faders just put the entire
/// useful range in the top of their travel — so the scale is pinned here.
struct MAudio1814LevelTests {
    /// The ALSA crate declares GAIN_TLV/VOLUME_TLV as
    /// `DbInterval { min: -12800, max: 0 }`, and TLV units are 0.01 dB. The
    /// device's own shell agrees: `fw vol` is documented as -128...0 dB.
    @Test func rawMapsToDecibelsAtOneDbPerStep() {
        #expect(MAudio1814Level.decibels(raw: 0) == 0)
        #expect(MAudio1814Level.decibels(raw: -32768) == -128)
        #expect(MAudio1814Level.decibels(raw: -256) == -1)
        #expect(MAudio1814Level.decibels(raw: -2560) == -10)
        // 0x100 is the crate's GAIN_STEP, which is therefore exactly one dB.
        #expect(MAudio1814Level.decibels(raw: -0x100) == -1)
    }

    @Test func decibelsRoundTripThroughRaw() {
        for db in stride(from: -120.0, through: 0.0, by: 0.5) {
            let raw = MAudio1814Level.raw(decibels: db)
            #expect(abs(MAudio1814Level.decibels(raw: raw) - db) < 0.01)
        }
    }

    @Test func clampsOutsideTheRegisterRange() {
        #expect(MAudio1814Level.raw(decibels: 12) == 0)
        #expect(MAudio1814Level.raw(decibels: -400) == -32768)
    }

    /// Unity sits at the top of the throw and the bottom is silence, not the
    /// fader floor — otherwise a fader pulled all the way down still passes
    /// audio at -60 dB.
    @Test func faderTravelSpansUnityToSilence() {
        #expect(MAudio1814Level.position(raw: 0) == 1)
        #expect(MAudio1814Level.raw(position: 1) == 0)
        #expect(MAudio1814Level.raw(position: 0) == MAudio1814Level.rawMinimum)
        #expect(MAudio1814Level.position(raw: MAudio1814Level.rawMinimum) == 0)
    }

    /// The regression this whole change is about: a linear-in-raw fader puts
    /// -64 dB at half travel. Half travel must be a usable monitoring level.
    @Test func midTravelIsAUsableLevelNotSixtyFourDbDown() {
        let midDb = MAudio1814Level.decibels(raw: MAudio1814Level.raw(position: 0.5))
        #expect(midDb == MAudio1814Level.faderFloorDb / 2)
        #expect(midDb > -35)
    }

    @Test func faderPositionsAreMonotonic() {
        var previous = -1.0
        for step in stride(from: 0.0, through: 1.0, by: 0.05) {
            let position = MAudio1814Level.position(raw: MAudio1814Level.raw(position: step))
            #expect(position >= previous)
            previous = position
        }
    }

    @Test func formatsSilenceRatherThanAMisleadingNumber() {
        #expect(MAudio1814Level.format(raw: MAudio1814Level.rawMinimum) == "-∞")
        #expect(MAudio1814Level.format(raw: 0) == "0.0")
        #expect(MAudio1814Level.format(raw: -2560) == "-10")
    }

    // MARK: - Meters

    /// Meters are **linear amplitude**, unlike the gain registers above. Values
    /// from a FireBug capture of the block during playback: analog out, headphone
    /// and aux out all read 0x37f7 / 0x39b4. Read as amplitude that is about
    /// -7 dBFS; read as a linear dB scale it would be -81 dB, and the whole
    /// console rendered empty because -81 sits below the display floor.
    @Test func metersAreLinearAmplitudeNotLinearInDecibels() {
        #expect(abs(MAudio1814Level.meterDecibels(raw: 0x37f7) - -7.2) < 0.1)
        #expect(abs(MAudio1814Level.meterDecibels(raw: 0x39b4) - -6.9) < 0.1)
        #expect(abs(MAudio1814Level.meterDecibels(raw: Int16.max) - 0) < 0.01)
        // Halving amplitude is -6 dB, which is what "linear amplitude" means.
        let half = Int16(Double(Int16.max) / 2)
        #expect(abs(MAudio1814Level.meterDecibels(raw: half) - -6.02) < 0.1)
    }

    @Test func silenceReadsAsTheFloor() {
        #expect(MAudio1814Level.meterDecibels(raw: 0) == MAudio1814Level.meterFloorDb)
        #expect(MAudio1814Level.meterPosition(raw: 0) == 0)
        #expect(MAudio1814Level.formatMeter(raw: 0) == "-∞")
    }

    /// The regression: a healthy playback level has to fill most of the bar.
    @Test func aPlayingSignalFillsMostOfTheMeter() {
        let position = MAudio1814Level.meterPosition(raw: 0x37f7)
        #expect(position > 0.8)
        #expect(position < 1.0)
        #expect(MAudio1814Level.formatMeter(raw: 0x37f7) == "-7")
    }

    @Test func meterFillIgnoresTheInaudibleBottomOfTheScale() {
        #expect(MAudio1814Level.meterPosition(raw: Int16.max) == 1)
        // -80 dBFS is below the display floor and must not light the bar.
        #expect(MAudio1814Level.meterPosition(raw: MAudio1814Level.rawMeter(decibels: -80)) == 0)
    }

    /// Only over the range the format can actually resolve: -60 dBFS is raw 33,
    /// and by -80 it is raw 3, where a single count is most of a decibel. That is
    /// a property of a 16-bit amplitude, not of the conversion.
    @Test func meterDecibelsRoundTripThroughRaw() {
        for db in stride(from: -60.0, through: 0.0, by: 2.5) {
            let raw = MAudio1814Level.rawMeter(decibels: db)
            #expect(abs(MAudio1814Level.meterDecibels(raw: raw) - db) < 0.2)
        }
    }

    // MARK: - Pan

    /// **Positive raw is LEFT.** The vendor's factory default puts the first
    /// channel of each pair at +32640 and prints `L127` under that channel's
    /// knob, so the register sign is inverted relative to the display.
    @Test func panIsCentredAtZeroAndPositiveRawIsLeft() {
        #expect(MAudio1814Level.panPosition(raw: 0) == 0)
        #expect(MAudio1814Level.panPosition(raw: 32_640) == -1)
        #expect(MAudio1814Level.panPosition(raw: -32_640) == 1)
        #expect(MAudio1814Level.rawPan(position: -1) == 32_640)
        #expect(MAudio1814Level.rawPan(position: 1) == -32_640)
        #expect(MAudio1814Level.rawPan(position: 0) == 0)
    }

    /// The vendor prints pan as L127/R127, and its hard-panned default has to
    /// come out reading the way its own panel does.
    @Test func panFormatsInTheVendorDisplayDomain() {
        #expect(MAudio1814Level.formatPan(raw: 0) == "C")
        #expect(MAudio1814Level.formatPan(raw: 32_640) == "L127")
        #expect(MAudio1814Level.formatPan(raw: -32_640) == "R127")
    }

    @Test func frontPanelRotaryCounterKeepsMovementAcrossWrap() {
        // 0xfc00 + one 0x400 detent wraps to zero. The UI must still observe
        // that as one positive detent rather than a large negative jump.
        #expect(MAudio1814FrontPanel.rotaryDelta(current: 0, previous: -1024) == 0x400)
        #expect(MAudio1814FrontPanel.rotaryDelta(current: -1024, previous: 0) == -0x400)
    }
}

/// The device already maintains an envelope — two reads 22 ms apart come back
/// byte-identical — so only the held peak is derived here.
struct AudioMeterPeakHoldTests {
    private func snapshot(_ values: [Int16]) -> AudioMeterSnapshot {
        AudioMeterSnapshot(
            endpointID: AudioEndpointID(rawValue: 1), topologyRevision: 1, telemetrySequence: 1,
            detectedSampleRateHz: 48_000, isEnabled: true, isClockLocked: true,
            isExternallySynced: false, hardwareSwitch: false, rotaries: [],
            values: values)
    }

    @Test func peakRisesInstantlyToASample() {
        var hold = AudioMeterPeakHold()
        hold.observe(snapshot([0]), now: 0)
        hold.observe(snapshot([Int16.max]), now: 0.02)
        #expect(hold.peak(at: 0) == Int16.max)
    }

    /// The marker has to outlive the signal, or a transient is unreadable.
    @Test func peakHoldsWhileTheSignalFalls() {
        var hold = AudioMeterPeakHold()
        hold.observe(snapshot([0]), now: 0)
        hold.observe(snapshot([Int16.max]), now: 0.02)
        hold.observe(snapshot([0]), now: 1.0)
        #expect(hold.peak(at: 0) == Int16.max)
    }

    @Test func peakFallsOnceItsHoldExpires() {
        var hold = AudioMeterPeakHold()
        hold.observe(snapshot([0]), now: 0)
        hold.observe(snapshot([Int16.max]), now: 0.02)
        hold.observe(snapshot([0]), now: 3.02)
        #expect(hold.peak(at: 0) < Int16.max)
    }

    /// A quiet channel must not be dragged up by a loud neighbour.
    @Test func channelsAreIndependent() {
        var hold = AudioMeterPeakHold()
        hold.observe(snapshot([0, 0]), now: 0)
        hold.observe(snapshot([Int16.max, 0]), now: 0.02)
        #expect(hold.peak(at: 0) == Int16.max)
        #expect(hold.peak(at: 1) == 0)
    }

    @Test func resetClearsEverything() {
        var hold = AudioMeterPeakHold()
        hold.observe(snapshot([Int16.max]), now: 0)
        hold.reset()
        #expect(hold.peak(at: 0) == 0)
    }
}
