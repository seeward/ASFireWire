// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialProtocol.hpp — FireWire 1814 and ProjectMix I/O.
//
// One class for both, because they are one device family running one firmware:
// Linux gives them the same quirk, the same forced reset, the same SpecialModel
// and the same control surface, and differs only in how many rates it offers
// (bebob.c:171-174, :285-293; maudio/special.rs:73 vs :89).
//
// What makes this device unlike every other BeBoB adapter here:
//
//  - **Its geometry is not discovered, it is asserted.** MAudioSpecialFormation
//    holds the table; nothing may ask the device to confirm it (H5).
//  - **Capture and playback shapes are chosen independently**, by dig_in_fmt and
//    dig_out_fmt in the vendor clock command, so DeviceCaps() depends on runtime
//    state rather than being a constant like Phase88Caps().
//  - **Only six AV/C frame shapes may ever be sent to it.** The permitted-frame
//    table in Protocols/AVC/AVCCommandFilter.hpp refuses everything else at
//    submit, including anything this class might send by mistake.
//
// The inverted start choreography (H8) is implemented by ConfirmDuplexStart:
// after host DMA is armed, this device receives the signal-format pair again.

#pragma once

#include "BeBoBProtocol.hpp"
#include "MAudioSpecialFormation.hpp"
#include "MAudioSpecialMeter.hpp"
#include "MAudioSpecialParameters.hpp"
#include "../Configuration/IAudioConfigurationControl.hpp"
#include "../../Shared/Controls/IAudioControlSurface.hpp"
#include "../../Shared/Metering/IAudioMetering.hpp"

#include <DriverKit/IOLib.h>

#include <cstdint>
#include <vector>

namespace ASFW::Audio::BeBoB {

/// M-Audio special-firmware parameter window, 0xffc700700000. Write-only: the
/// firmware answers no read here, so the host must assert every register.
/// Register map in FFADO bebob/maudio/special_avdevice.h:47-134.
inline constexpr uint16_t kMAudioParamAddressHi = 0xFFC7;
inline constexpr uint32_t kMAudioParamAddressLo = 0x0070'0000;

/// Which of the two personas this instance is driving. They share geometry and
/// control; only the offered rate list differs.
enum class MAudioSpecialModel : uint8_t {
    FireWire1814,
    ProjectMix,
};

class MAudioSpecialProtocol final : public BeBoBProtocol,
                                   public IAudioConfigurationControl,
                                   public IAudioControlSurface,
                                   public IAudioMetering {
public:
    MAudioSpecialProtocol(Protocols::Ports::FireWireBusOps& busOps,
                          Protocols::Ports::FireWireBusInfo& busInfo,
                          Discovery::DeviceRouteToken route,
                          IRM::IRMClient* irmClient,
                          CMP::CMPClient* cmpClient,
                          Scheduling::ITimerScheduler* timerScheduler,
                          MAudioSpecialModel model) noexcept;
    ~MAudioSpecialProtocol() noexcept override;
    IOReturn Shutdown() override;

    const char* GetName() const override { return DeviceName(); }
    bool GetRuntimeAudioStreamCaps(AudioStreamRuntimeCaps& outCaps) const override;
    IAudioConfigurationControl* AsAudioConfigurationControl() noexcept override {
        return this;
    }
    const IAudioConfigurationControl* AsAudioConfigurationControl() const noexcept override {
        return this;
    }
    IAudioControlSurface* AsAudioControlSurface() noexcept override { return this; }
    const IAudioControlSurface* AsAudioControlSurface() const noexcept override { return this; }
    IAudioMetering* AsAudioMetering() noexcept override { return this; }
    const IAudioMetering* AsAudioMetering() const noexcept override { return this; }
    [[nodiscard]] bool SupportsConfiguration(
        const Configuration::DeviceConfiguration& configuration) const noexcept override;
    void ApplyConfiguration(const Configuration::DeviceConfiguration& configuration,
                            IAudioConfigurationControl::ApplyCallback callback) override;
    [[nodiscard]] AudioConfigurationApplyResult
    CurrentConfiguration() const noexcept override;
    [[nodiscard]] bool CopyAudioControlSurfaceSnapshot(
        AudioControlSurfaceSnapshot& outSnapshot) const noexcept override;
    void ApplyAudioControlValue(uint32_t controlId, int32_t value,
                                IAudioControlSurface::ApplyCallback callback) override;
    [[nodiscard]] bool CopyAudioMeterSnapshot(
        AudioMeterSnapshot& outSnapshot) const noexcept override;
    [[nodiscard]] IOReturn SetAudioMeteringEnabled(bool enabled) noexcept override;

    /// Tell the device which clock to run on and which digital formats are
    /// selected, then wait out its settle. Must complete before streaming.
    ///
    /// This reproduces the vendor driver's SetBlankSlateClockSource: the
    /// M-Audio clock/formation frame, 300 ms, Audio selector FB 4, 300 ms, then
    /// the outer 2500 ms blank-slate settle. It lives here rather than in
    /// ApplyClockConfig because that sequence must finish before publication.
    void InitializeClock(std::function<void(IOReturn)> completion);

protected:
    const char* DeviceName() const override;
    [[nodiscard]] AudioStreamRuntimeCaps DeviceCaps() const override;
    [[nodiscard]] std::vector<uint32_t> SupportedRates() const override;
    void ReadClockHealth(HealthCallback callback) override;

    /// This firmware fails the INPUT signal-format command when it lands
    /// immediately after the OUTPUT one. Linux's special_set_rate() waits
    /// exactly 100 ms. The vendor kext's 300 ms waits surround a different
    /// operation (its clock/selector pair), so they do not apply here.
    [[nodiscard]] uint32_t SignalFormatInterlockMs() const override { return 100; }

    /// The start choreography is inverted here: signal format must be re-sent
    /// *after* host DMA is running, not only before CMP. See the definition.
    void ConfirmDuplexStart(ConfirmCallback callback) override;

private:
    /// Assert the whole cached 0x00-0x9c parameter window in one block write.
    ///
    /// These registers are write-only: the firmware answers no read for them,
    /// so their power-on state is indeterminate and every quadlet must be
    /// stated. FFADO does exactly this at startup (Mixer::initialize,
    /// bebob/maudio/special_mixer.cpp:74-106) and drives this device; the
    /// vendor kext reaches the same end state through 42 individual per-control
    /// writes, which is its control API, not a wire requirement.
    ///
    /// Runs as the last step of InitializeClock, mirroring the vendor's
    /// non-blank-slate pass, where the equivalent register push happens
    /// (FWSettingsLevels::SendToDevice).
    void SendParameterBlock(std::function<void(IOReturn)> completion);
    void SendParameterQuadlet(size_t index, uint32_t value,
                              IAudioControlSurface::ApplyCallback completion);

    /// Turns front-panel knob detents into headphone-volume writes.
    ///
    /// The 1814's knobs are relative encoders that attenuate nothing on their
    /// own — the host is what applies them. The vendor kext binds byte 1 of the
    /// meter block to headphone pair 1 and byte 2 to pair 2
    /// (`ReceiveControlPacket` @ 0xe9ea), and that binding is fixed. Byte 3's
    /// knob is user-assignable across five level groups through a mask we have
    /// no reading of, so its detents are reported and not acted on.
    void ApplyRotaryDetents(const MAudio1814RotaryDelta& deltas) noexcept;

    /// Issues one pending knob-driven quadlet write, if the single-writer slot
    /// is free. Re-entered from each write completion until the mask drains.
    void FlushPendingParameterWrites() noexcept;

    /// TODO(1814 A/B switch): the momentary switch currently drives nothing but
    /// the lamp below, so pressing it changes a light and nothing else. In the
    /// vendor driver it is a monitor A/B for **headphone pair 1 only**:
    ///
    ///   - `MomentarySwitchPressed` @ 0x1e54c toggles a mode between 0 and 1,
    ///     re-sends the routing, then updates the LED.
    ///   - `SendMixerSettingsForCurrentMomentaryMode` @ 0x1e36a reads
    ///     `MAMomentarySwitchRoutings[mode]` and hands it to
    ///     `SubmitHeadphoneSourceChange` @ 0x1e2e2, which writes the headphone
    ///     selector for pair index 0. Pair 2 is independent
    ///     (`SetHeadphoneSecondSource` @ 0x1ddfe).
    ///   - `SourceMaskToHeadphoneSelector` @ 0x1dda0 is the index of the lowest
    ///     set bit, and `ResetToFactorySettings` @ 0x1df88 seeds the routings to
    ///     `[1, 2, 3, 4]`. So the factory A/B is **Mixer 1 <-> Mixer 2**, with
    ///     entry 3 (value 4 -> selector 2) reaching Aux if reassigned.
    ///
    /// It needs no new device surface: the target is register 0x98, which the
    /// headphone-source control already writes. The two presets are host state,
    /// like mute/solo/ctrl.
    ///
    /// **Do not implement the other branch** of
    /// `SendMixerSettingsForCurrentMomentaryMode`. It is gated on a config flag
    /// we do not model and manipulates a vendor `MAInputRoutingV2` property with
    /// unexplained bit twiddling (`| 0x12`); it has no analogue in our topology.
    ///
    /// Mirrors the front-panel switch onto the front-panel LED.
    ///
    /// The lamp is not autonomous either: the device reports the button and the
    /// host decides what the LED shows. The ALSA runtime does exactly this and
    /// nothing else with it — on a change of the polled switch bit, send the new
    /// state (runtime/bebob/src/maudio/special_model.rs:159-163).
    void SendLedState(bool illuminated) noexcept;
    void ScheduleMeterRead(uint64_t delayNs, uint64_t epoch) noexcept;
    void PollMeter(uint64_t epoch) noexcept;
    void CompleteMeterRead(uint64_t epoch, Discovery::DeviceRouteToken issuedRoute,
                           Async::AsyncStatus status,
                           std::span<const uint8_t> payload) noexcept;

    [[nodiscard]] AudioStreamRuntimeCaps CapsForCurrentFormation() const noexcept;

    const MAudioSpecialModel model_;

    // Mirrors Linux's `struct special_params`. These are driver-side beliefs
    // about device state, not readbacks — the device is never asked to confirm
    // them, so they must only ever change alongside a vendor clock command that
    // actually succeeded.
    //
    // Both default to S/PDIF because the vendor blank-slate frame and Linux's
    // discover-time frame agree on dig_in_fmt=0 and dig_out_fmt=0, despite
    // differing on clk_src (vendor 0, Linux 3).
    MAudioDigitalFormat captureFormat_{MAudioDigitalFormat::SPDIF};
    MAudioDigitalFormat playbackFormat_{MAudioDigitalFormat::SPDIF};

    // The rate the geometry is currently shaped for. Seeded from the device via
    // the signal-format probe rather than assumed, because this firmware keeps
    // its rate across a host restart and guessing wrong mis-shapes the stream.
    uint32_t currentRateHz_{48000};

    // The special firmware's parameter window is write-only. Keep the value we
    // have actually written separately from the latest desired image: rotary
    // detents may arrive while a quadlet write is outstanding. Only the
    // confirmed image is ever published as driver belief.
    IOLock* parameterLock_{nullptr};
    MAudioSpecialParameterImage confirmedParameterImage_{};
    MAudioSpecialParameterImage desiredParameterImage_{};
    uint32_t parameterRevision_{1};
    bool parameterWriteInFlight_{false};
    /// Quadlets where the desired image has advanced beyond the confirmed image.
    /// A detent is recorded immediately so none is lost while a write is
    /// outstanding, but it is not presented as confirmed until its write ACK.
    uint64_t pendingParameterQuadlets_{0};
    static_assert(MAudioSpecialParameterImage::kQuadletCount <= 64,
                  "pendingParameterQuadlets_ is a 64-bit mask over the window");

    IOLock* meterLock_{nullptr};
    MAudioSpecialMeterState meterState_{};
    uint32_t meterRevision_{0};
    uint64_t meterEpoch_{0};
    Scheduling::TimerToken meterTimer_{Scheduling::kInvalidTimerToken};
    bool meterEnabled_{false};
    bool meterReadInFlight_{false};
    uint64_t meterReadEpoch_{0};
    /// Last switch state we lit the LED for. Starts unset so the first confirmed
    /// block establishes the lamp rather than assuming it powered up dark.
    bool ledState_{false};
    bool ledStateKnown_{false};
};

} // namespace ASFW::Audio::BeBoB
