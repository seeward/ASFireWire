// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialProtocol.cpp — FireWire 1814 / ProjectMix I/O.
//
// Fresh implementation. Geometry and choreography are cross-validated with
// Linux sound/firewire/bebob/bebob_maudio.c; no reference source is copied.

#include "MAudioSpecialProtocol.hpp"

#include "MAudioClockCommand.hpp"

#include "../../../Logging/Logging.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <utility>

namespace ASFW::Audio::BeBoB {

MAudioSpecialProtocol::MAudioSpecialProtocol(
    Protocols::Ports::FireWireBusOps& busOps,
    Protocols::Ports::FireWireBusInfo& busInfo,
    Discovery::DeviceRouteToken route,
    IRM::IRMClient* irmClient,
    CMP::CMPClient* cmpClient,
    Scheduling::ITimerScheduler* timerScheduler,
    MAudioSpecialModel model) noexcept
    : BeBoBProtocol(busOps, busInfo, route, irmClient, cmpClient, timerScheduler),
      model_(model) {
    parameterLock_ = IOLockAlloc();
    if (!parameterLock_) {
        ASFW_LOG_ERROR(Audio, "[MAudioControl] parameter lock allocation failed");
    }
    meterLock_ = IOLockAlloc();
    if (!meterLock_) {
        ASFW_LOG_ERROR(Audio, "[MAudioMeter] meter lock allocation failed");
    }
}

MAudioSpecialProtocol::~MAudioSpecialProtocol() noexcept {
    (void)SetAudioMeteringEnabled(false);
    if (meterLock_) {
        IOLockFree(meterLock_);
        meterLock_ = nullptr;
    }
    if (parameterLock_) {
        IOLockFree(parameterLock_);
        parameterLock_ = nullptr;
    }
}

IOReturn MAudioSpecialProtocol::Shutdown() {
    (void)SetAudioMeteringEnabled(false);
    return BeBoBProtocol::Shutdown();
}

const char* MAudioSpecialProtocol::DeviceName() const {
    switch (model_) {
        case MAudioSpecialModel::FireWire1814:
            return "M-Audio FireWire 1814";
        case MAudioSpecialModel::ProjectMix:
            return "M-Audio ProjectMix I/O";
    }
    return "M-Audio BeBoB";
}

std::vector<uint32_t> MAudioSpecialProtocol::SupportedRates() const {
    // Same table for both, truncated for ProjectMix. Linux expresses this as
    // `max -= 2` over the shared formation loop rather than a second table
    // (bebob_maudio.c:241-243).
    const size_t count = model_ == MAudioSpecialModel::FireWire1814
                             ? kMAudioFireWire1814RateCount
                             : kMAudioProjectMixRateCount;
    return std::vector<uint32_t>(kMAudioSpecialRatesHz,
                                 kMAudioSpecialRatesHz + count);
}

AudioStreamRuntimeCaps MAudioSpecialProtocol::CapsForCurrentFormation() const noexcept {
    AudioStreamRuntimeCaps caps{};

    const auto formation =
        MAudioFormationFor(captureFormat_, playbackFormat_, currentRateHz_);
    if (!formation) {
        // Only reachable if currentRateHz_ was set to something SupportedRates()
        // never offered. Report empty rather than a plausible-looking shape: a
        // wrong geometry here is silent on the wire, and zero is not.
        ASFW_LOG_ERROR(Audio,
                       "[BeBoB] %{public}s: no formation for rate=%u — geometry unset",
                       DeviceName(), currentRateHz_);
        return caps;
    }

    // DBS is PCM plus the MIDI conformant-data block, in each direction
    // independently. bebob_maudio.c:248-253.
    const uint32_t captureSlots =
        formation->capturePcmChannels + formation->midiDataBlocks;
    const uint32_t playbackSlots =
        formation->playbackPcmChannels + formation->midiDataBlocks;

    // Aggregate (HAL) view.
    caps.hostInputPcmChannels = formation->capturePcmChannels;
    caps.hostOutputPcmChannels = formation->playbackPcmChannels;
    caps.deviceToHostAm824Slots = captureSlots;
    caps.hostToDeviceAm824Slots = playbackSlots;
    caps.sampleRateHz = currentRateHz_;
    caps.deviceToHostIsoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;
    caps.hostToDeviceIsoChannel = AudioStreamRuntimeCaps::kInvalidIsoChannel;

    // Per-stream (wire) view. Both must be filled: the aggregate view alone
    // leaves the transport with no stream to arm and StartIO fails.
    caps.deviceToHostStreamCount = 1;
    caps.hostToDeviceStreamCount = 1;
    caps.deviceToHostStreams[0] = {
        .pcmChannels = static_cast<uint16_t>(formation->capturePcmChannels),
        .am824Slots = static_cast<uint16_t>(captureSlots)};
    caps.hostToDeviceStreams[0] = {
        .pcmChannels = static_cast<uint16_t>(formation->playbackPcmChannels),
        .am824Slots = static_cast<uint16_t>(playbackSlots)};
    return caps;
}

AudioStreamRuntimeCaps MAudioSpecialProtocol::DeviceCaps() const {
    return CapsForCurrentFormation();
}

bool MAudioSpecialProtocol::GetRuntimeAudioStreamCaps(
    AudioStreamRuntimeCaps& outCaps) const {
    outCaps = CapsForCurrentFormation();
    return outCaps.sampleRateHz != 0;
}

bool MAudioSpecialProtocol::SupportsConfiguration(
    const Configuration::DeviceConfiguration& configuration) const noexcept {
    // The profile deliberately limits the first production coordinator backend
    // to the two base-rate formations. Both personas carry them: the formation
    // table is shared and only its rate *count* differs (ProjectMix stops after
    // 96 kHz), so 44.1/48 geometry is identical for the two.
    return (configuration.sampleRate == 44100 || configuration.sampleRate == 48000) &&
           configuration.opticalInput.has_value() &&
           configuration.opticalOutput.has_value();
}

bool MAudioSpecialProtocol::CopyAudioControlSurfaceSnapshot(
    AudioControlSurfaceSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    // The parameter window and the meter block are shared by both personas, not
    // 1814-specific: `com_m_audio_FWProjectMixDevice` derives from
    // `com_m_audio_FW1814Device` and overrides only geometry, the capture
    // converter, digital-signal switching, factory reset and engine setup —
    // never the mixer, metering or control-packet path. ALSA agrees, defining
    // both as `SpecialModel<T>` parameterised solely by the clock protocol
    // (runtime/bebob/src/maudio/special_model.rs:10-11).
    if (!parameterLock_) return false;

    // Enumerated from the range table rather than a hand-kept list, so a group
    // added there cannot be silently missing from the surface. The whole window
    // is one snapshot: kMaxAudioControlSurfaceValues is sized to hold it.
    static_assert(kMAudio1814ControlCount <= kMaxAudioControlSurfaceValues,
                  "the 1814 control surface no longer fits one snapshot");

    IOLockLock(parameterLock_);
    outSnapshot.kind = AudioControlSurfaceKind::MAudio1814Mixer;
    outSnapshot.stateRevision = parameterRevision_;
    size_t emitted = 0;
    for (const auto& group : kMAudio1814ControlGroups) {
        for (uint32_t index = 0; index < group.count; ++index) {
            const uint32_t id = MakeMAudio1814ControlId(group.group, index);
            outSnapshot.values[emitted++] = {
                .id = id,
                .value = confirmedParameterImage_.ControlValue(id),
            };
        }
    }
    outSnapshot.valueCount = static_cast<uint32_t>(emitted);
    IOLockUnlock(parameterLock_);
    return true;
}

bool MAudioSpecialProtocol::CopyAudioSemanticConsoleLayout(
    AudioSemanticConsoleLayoutSnapshot& outSnapshot) const noexcept {
    return BuildMAudioSpecialConsoleLayout(captureFormat_, outSnapshot);
}

bool MAudioSpecialProtocol::CopyAudioMeterSnapshot(
    AudioMeterSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    if (!meterLock_) return false;

    IOLockLock(meterLock_);
    outSnapshot.telemetrySequence = meterRevision_;
    outSnapshot.valueCount = static_cast<uint32_t>(meterState_.peaks.size());
    outSnapshot.detectedSampleRateHz = meterState_.detectedSampleRateHz;
    outSnapshot.enabled = meterEnabled_;
    outSnapshot.clockLocked = meterState_.clockLocked;
    outSnapshot.externalSync = meterState_.externalSync;
    outSnapshot.hardwareSwitch = meterState_.hardwareSwitch;
    outSnapshot.rotaryCount = static_cast<uint32_t>(meterState_.rotaries.size());
    for (size_t i = 0; i < meterState_.rotaries.size(); ++i) {
        outSnapshot.rotaries[i] = std::bit_cast<int16_t>(meterState_.rotaries[i]);
    }
    for (size_t i = 0; i < meterState_.peaks.size(); ++i) {
        outSnapshot.values[i] = meterState_.peaks[i];
    }
    IOLockUnlock(meterLock_);
    return true;
}

IOReturn MAudioSpecialProtocol::SetAudioMeteringEnabled(bool enabled) noexcept {
    if (!meterLock_) {
        return kIOReturnUnsupported;
    }

    Scheduling::TimerToken cancelled = Scheduling::kInvalidTimerToken;
    uint64_t epoch = 0;
    IOLockLock(meterLock_);
    if (meterEnabled_ == enabled) {
        IOLockUnlock(meterLock_);
        return kIOReturnSuccess;
    }
    meterEnabled_ = enabled;
    epoch = ++meterEpoch_;
    cancelled = std::exchange(meterTimer_, Scheduling::kInvalidTimerToken);
    if (enabled) {
        // Any older completion is epoch-stamped and must not block a newly
        // enabled poller, nor overwrite the new epoch's in-flight marker.
        meterReadInFlight_ = false;
        meterReadEpoch_ = 0;
    }
    if (!enabled) {
        meterState_.clockLocked = false;
        meterState_.externalSync = false;
        // The encoders integrate detents; a gap in polling loses turns, so the
        // next enable starts a fresh run rather than continuing a stale total.
        meterState_.hasPreviousEvents = false;
        // Re-establish the lamp on the next enable rather than trusting a
        // belief formed before the gap.
        ledStateKnown_ = false;
        ++meterRevision_;
    }
    IOLockUnlock(meterLock_);
    if (cancelled != Scheduling::kInvalidTimerToken && timerScheduler_) {
        timerScheduler_->Cancel(cancelled);
    }

    ASFW_LOG(Audio, "[MAudioMeter] enabled=%u epoch=%llu", enabled ? 1U : 0U,
             static_cast<unsigned long long>(epoch));
    if (enabled) ScheduleMeterRead(0, epoch);
    return kIOReturnSuccess;
}

void MAudioSpecialProtocol::ScheduleMeterRead(uint64_t delayNs, uint64_t epoch) noexcept {
    if (!timerScheduler_) {
        ASFW_LOG_ERROR(Audio, "[MAudioMeter] unavailable: no timer scheduler");
        return;
    }
    const auto token = timerScheduler_->ScheduleAfter(delayNs, [this, epoch] {
        PollMeter(epoch);
    });
    if (token == Scheduling::kInvalidTimerToken) {
        ASFW_LOG_ERROR(Audio, "[MAudioMeter] timer scheduling failed");
        return;
    }

    bool keep = false;
    IOLockLock(meterLock_);
    if (meterEnabled_ && meterEpoch_ == epoch &&
        meterTimer_ == Scheduling::kInvalidTimerToken) {
        meterTimer_ = token;
        keep = true;
    }
    IOLockUnlock(meterLock_);
    if (!keep) timerScheduler_->Cancel(token);
}

void MAudioSpecialProtocol::PollMeter(uint64_t epoch) noexcept {
    Discovery::DeviceRouteToken issuedRoute{};
    IOLockLock(meterLock_);
    meterTimer_ = Scheduling::kInvalidTimerToken;
    if (!meterEnabled_ || meterEpoch_ != epoch || meterReadInFlight_) {
        IOLockUnlock(meterLock_);
        return;
    }
    meterReadInFlight_ = true;
    meterReadEpoch_ = epoch;
    issuedRoute = route_;
    IOLockUnlock(meterLock_);

    const auto operationalNode = Discovery::TryOperationalNodeId(issuedRoute.nodeId);
    if (!operationalNode) {
        CompleteMeterRead(epoch, issuedRoute, Async::AsyncStatus::kAborted, {});
        return;
    }

    const Async::FWAddress address{Async::FWAddress::QualifiedAddressParts{
        .addressHi = kMAudioParamAddressHi,
        .addressLo = 0x0060'0000U,
        .nodeID = issuedRoute.nodeId}};
    (void)busOps_.ReadBlock(
        issuedRoute.generation, FW::NodeId{*operationalNode}, address,
        MAudioSpecialMeterState::kBlockBytes, FW::FwSpeed::S100,
        [this, epoch, issuedRoute](Async::AsyncStatus status,
                                   std::span<const uint8_t> payload) {
            CompleteMeterRead(epoch, issuedRoute, status, payload);
        });
}

void MAudioSpecialProtocol::CompleteMeterRead(
    uint64_t epoch, Discovery::DeviceRouteToken issuedRoute,
    Async::AsyncStatus status, std::span<const uint8_t> payload) noexcept {
    // 50 Hz. The block is an 84-byte async read, so the bus cost is trivial, and
    // anything slower aliases against the client's own poll: two independent
    // timers at the same period means the client repeatedly sees the same
    // revision and then skips one, which reads as stutter rather than as a slow
    // meter. Sampling faster than the reader is what removes that.
    constexpr uint64_t kPollNs = 20ULL * 1000ULL * 1000ULL;
    constexpr uint64_t kRetryNs = 1000ULL * 1000ULL * 1000ULL;
    bool accepted = false;
    bool decoded = false;
    bool clockStateChanged = false;
    uint32_t detectedRateHz = 0;
    bool clockLocked = false;
    bool externalSync = false;
    bool switchChanged = false;
    bool switchState = false;
    MAudio1814RotaryDelta rotaryDeltas{};
    uint64_t nextDelayNs = kRetryNs;

    IOLockLock(meterLock_);
    if (meterReadInFlight_ && meterReadEpoch_ == epoch) {
        meterReadInFlight_ = false;
        meterReadEpoch_ = 0;
    }
    const bool active = meterEnabled_ && meterEpoch_ == epoch;
    const bool sameRoute = route_.generation == issuedRoute.generation &&
                           route_.nodeId == issuedRoute.nodeId;
    if (active && sameRoute && status == Async::AsyncStatus::kSuccess) {
        // Decoded in place: the rotary and switch fields are integrated from
        // edge events, so the previous block's event bytes must survive.
        const uint32_t previousRateHz = meterState_.detectedSampleRateHz;
        const bool previousLocked = meterState_.clockLocked;
        const bool previousExternal = meterState_.externalSync;
        if (DecodeMAudioSpecialMeter(payload, meterState_, &rotaryDeltas)) {
            switchState = meterState_.hardwareSwitch;
            switchChanged = !ledStateKnown_ || ledState_ != switchState;
            ledState_ = switchState;
            ledStateKnown_ = true;
            clockStateChanged = meterRevision_ == 0 ||
                previousRateHz != meterState_.detectedSampleRateHz ||
                previousLocked != meterState_.clockLocked ||
                previousExternal != meterState_.externalSync;
            ++meterRevision_;
            accepted = true;
            decoded = true;
            detectedRateHz = meterState_.detectedSampleRateHz;
            clockLocked = meterState_.clockLocked;
            externalSync = meterState_.externalSync;
            nextDelayNs = kPollNs;
        }
    }
    const bool reschedule = meterEnabled_ && meterEpoch_ == epoch;
    IOLockUnlock(meterLock_);

    if (accepted) {
        // Outside the meter lock: these take the parameter lock and may start an
        // async write.
        if (rotaryDeltas.Any()) {
            ApplyRotaryDetents(rotaryDeltas);
        }
        if (switchChanged) {
            SendLedState(switchState);
        }
        // Also drain unconditionally. A knob write that failed re-armed its
        // quadlet and has nobody else to retry it; the poll is the one thing
        // that keeps running. No-op when nothing is pending.
        FlushPendingParameterWrites();
        if (clockStateChanged) {
            ASFW_LOG(Audio,
                     "[MAudioMeter] clock rate=%u locked=%u external=%u node=0x%04x gen=%u",
                     detectedRateHz, clockLocked ? 1U : 0U, externalSync ? 1U : 0U,
                     static_cast<unsigned>(issuedRoute.nodeId),
                     static_cast<unsigned>(issuedRoute.generation.value));
        }
    } else if (reschedule) {
        ASFW_LOG_ERROR(Audio,
                       "[MAudioMeter] read rejected status=%u bytes=%zu routeCurrent=%u decoded=%u; retry=1s",
                       static_cast<unsigned>(status), payload.size(),
                       sameRoute ? 1U : 0U, decoded ? 1U : 0U);
    }
    if (reschedule) ScheduleMeterRead(nextDelayNs, epoch);
}

void MAudioSpecialProtocol::ApplyAudioControlValue(
    uint32_t controlId, int32_t value, IAudioControlSurface::ApplyCallback callback) {
    if (!callback) return;
    if (!parameterLock_) {
        callback(kIOReturnUnsupported);
        return;
    }

    size_t changedIndex = 0;
    uint32_t changedValue = 0;
    IOLockLock(parameterLock_);
    if (parameterWriteInFlight_) {
        IOLockUnlock(parameterLock_);
        callback(kIOReturnBusy);
        return;
    }
    MAudioSpecialParameterImage proposed = desiredParameterImage_;
    if (!proposed.Apply(controlId, value, changedIndex)) {
        IOLockUnlock(parameterLock_);
        callback(kIOReturnBadArgument);
        return;
    }
    changedValue = proposed.QuadletAt(changedIndex);
    desiredParameterImage_ = proposed;
    parameterWriteInFlight_ = true;
    IOLockUnlock(parameterLock_);

    ASFW_LOG(Audio,
             "[MAudioControl] request id=%u value=%d offset=0x%02zx quadlet=0x%08x "
             "node=0x%04x gen=%u",
             controlId, value, changedIndex * sizeof(uint32_t), changedValue,
             static_cast<unsigned>(route_.nodeId), static_cast<unsigned>(route_.generation.value));
    SendParameterQuadlet(
        changedIndex, changedValue,
        [this, controlId, value, changedIndex, changedValue,
         callback = std::move(callback)](IOReturn status) mutable {
            if (parameterLock_) {
                IOLockLock(parameterLock_);
                parameterWriteInFlight_ = false;
                if (status == kIOReturnSuccess) {
                    // Advance exactly the quadlet the device acknowledged. A
                    // rotary may have changed the desired image since this
                    // write was issued, so do not copy that later value here.
                    if (confirmedParameterImage_.QuadletAt(changedIndex) != changedValue) {
                        (void)confirmedParameterImage_.SetQuadlet(changedIndex, changedValue);
                        ++parameterRevision_;
                    }
                    if (desiredParameterImage_.QuadletAt(changedIndex) != changedValue) {
                        pendingParameterQuadlets_ |= (1ULL << changedIndex);
                    }
                } else if (desiredParameterImage_.QuadletAt(changedIndex) == changedValue) {
                    // No later desired value superseded this failed request, so
                    // discard it rather than letting a rejected UI action turn
                    // into an invisible retry. A newer rotary value remains
                    // pending and will still be flushed below.
                    (void)desiredParameterImage_.SetQuadlet(
                        changedIndex, confirmedParameterImage_.QuadletAt(changedIndex));
                }
                IOLockUnlock(parameterLock_);
            }
            FlushPendingParameterWrites();
            if (status == kIOReturnSuccess) {
                ASFW_LOG(Audio,
                         "[MAudioControl] confirmed-belief id=%u value=%d offset=0x%02zx",
                         controlId, value, changedIndex * sizeof(uint32_t));
            } else {
                ASFW_LOG_ERROR(Audio,
                               "[MAudioControl] rejected id=%u value=%d offset=0x%02zx kr=0x%08x",
                               controlId, value, changedIndex * sizeof(uint32_t), status);
            }
            callback(status);
        });
}

AudioConfigurationApplyResult MAudioSpecialProtocol::CurrentConfiguration() const noexcept {
    return {
        .configuration = {
            .sampleRate = currentRateHz_,
            .opticalInput = captureFormat_ == MAudioDigitalFormat::ADAT
                ? Configuration::OpticalMode::Adat : Configuration::OpticalMode::Spdif,
            .opticalOutput = playbackFormat_ == MAudioDigitalFormat::ADAT
                ? Configuration::OpticalMode::Adat : Configuration::OpticalMode::Spdif,
        },
        .runtimeCaps = CapsForCurrentFormation(),
    };
}

void MAudioSpecialProtocol::ApplyConfiguration(
    const Configuration::DeviceConfiguration& configuration,
    IAudioConfigurationControl::ApplyCallback callback) {
    if (!callback) {
        return;
    }
    if (!SupportsConfiguration(configuration)) {
        callback(kIOReturnUnsupported, {});
        return;
    }
    if (!fcpTransport_) {
        callback(kIOReturnNotReady, {});
        return;
    }

    const MAudioDigitalFormat capture =
        *configuration.opticalInput == Configuration::OpticalMode::Adat
            ? MAudioDigitalFormat::ADAT : MAudioDigitalFormat::SPDIF;
    const MAudioDigitalFormat playback =
        *configuration.opticalOutput == Configuration::OpticalMode::Adat
            ? MAudioDigitalFormat::ADAT : MAudioDigitalFormat::SPDIF;
    const auto frame = BuildMAudioClockCommand(
        MAudioClockSource::Internal,
        capture == MAudioDigitalFormat::ADAT
            ? MAudioClockDigitalFormat::ADAT : MAudioClockDigitalFormat::SPDIF,
        playback == MAudioDigitalFormat::ADAT
            ? MAudioClockDigitalFormat::ADAT : MAudioClockDigitalFormat::SPDIF,
        /*lockSettings=*/false);
    Protocols::AVC::FCPFrame command{};
    command.length = frame.size();
    std::memcpy(command.data.data(), frame.data(), frame.size());

    // The special clock command owns the independent dig_in_fmt/dig_out_fmt
    // selectors (bebob_maudio.c:166-216). Only after its accepted response do
    // we update our write-only-register belief, then use the shared BeBoB
    // OUTPUT -> 100 ms -> INPUT rate sequence (bebob_maudio.c:301-339).
    ASFW_LOG(Audio,
             "[MAudioConfig] FCP special-clock submit rate=%u input=%{public}s output=%{public}s "
             "ctype=0x%02x subunit=0x%02x opcode=0x%02x frame=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             configuration.sampleRate,
             capture == MAudioDigitalFormat::ADAT ? "ADAT" : "S/PDIF",
             playback == MAudioDigitalFormat::ADAT ? "ADAT" : "S/PDIF",
             command.data[0], command.data[1], command.data[2], command.data[0],
             command.data[1], command.data[2], command.data[3], command.data[4],
             command.data[5], command.data[6], command.data[7], command.data[8],
             command.data[9], command.data[10], command.data[11]);
    const auto handle = fcpTransport_->SubmitCommand(
        command,
        [this, configuration, capture, playback,
         callback = std::move(callback)](Protocols::AVC::FCPStatus status,
                                         const Protocols::AVC::FCPFrame& response) mutable {
            const uint8_t responseCode = response.length > 0 ? response.data[0] : 0xFFU;
            if (status != Protocols::AVC::FCPStatus::kOk ||
                (responseCode != 0x09U && responseCode != 0x0CU)) {
                ASFW_LOG_ERROR(Audio,
                               "[MAudioConfig] clock command rejected status=%u response=0x%02x",
                               static_cast<unsigned>(status), responseCode);
                callback(kIOReturnIOError, {});
                return;
            }
            ASFW_LOG(Audio,
                     "[MAudioConfig] FCP special-clock reply status=%u response=0x%02x bytes=%zu",
                     static_cast<unsigned>(status), responseCode, response.length);

            captureFormat_ = capture;
            playbackFormat_ = playback;
            currentRateHz_ = configuration.sampleRate;
            BeBoBProtocol::ApplyClockConfig(
                {.sampleRateHz = configuration.sampleRate},
                [this, configuration, callback = std::move(callback)](
                    IOReturn applyStatus, ClockApplyResult) mutable {
                    if (applyStatus != kIOReturnSuccess) {
                        ASFW_LOG_ERROR(Audio,
                                       "[MAudioConfig] signal-format apply failed rate=%u kr=0x%x",
                                       configuration.sampleRate, applyStatus);
                        callback(applyStatus, {});
                        return;
                    }
                    auto result = CurrentConfiguration();
                    ASFW_LOG(Audio,
                             "[MAudioConfig] apply confirmed-belief rate=%u input=%{public}s output=%{public}s hostIn=%u hostOut=%u",
                             result.configuration.sampleRate,
                             result.configuration.opticalInput == Configuration::OpticalMode::Adat
                                 ? "ADAT" : "S/PDIF",
                             result.configuration.opticalOutput == Configuration::OpticalMode::Adat
                                 ? "ADAT" : "S/PDIF",
                             result.runtimeCaps.hostInputPcmChannels,
                             result.runtimeCaps.hostOutputPcmChannels);
                    callback(kIOReturnSuccess, result);
                });
        });
    (void)handle;
}

void MAudioSpecialProtocol::InitializeClock(std::function<void(IOReturn)> completion) {
    if (!fcpTransport_) {
        completion(kIOReturnNotReady);
        return;
    }

    // Linux's discover-time policy, adopted whole: clock source 3 and no
    // selector.
    //
    // Two self-consistent policies configure this firmware, and ASFW previously
    // ran neither. The vendor kext sends operand 0 ("Internal with Digital
    // Mute") and follows it ~300 ms later with selector FB 4 — into an output
    // plug that is already connected and streaming. Linux sends operand 3
    // ("Internal") at discovery and never sends a selector at all
    // (bebob_maudio.c:276).
    //
    // Under operand 0 that selector is not decoration but the release: in the
    // original-driver FireBug capture (tools/1814/firebug.txt) the clock frame
    // alone drives the device's output stream from full-size packets down to
    // header-only ones (Largest 552 -> 8), and only the selector restores it
    // (-> 360, the S/PDIF capture formation). We were sending operand 0 with the
    // selector, but both before the plugs are connected, so nothing was ever
    // released. Operand 3 needs no release, which is why Linux ships without one.
    const auto frame = BuildMAudioClockCommand(
        MAudioClockSource::Internal,
        captureFormat_ == MAudioDigitalFormat::ADAT
            ? MAudioClockDigitalFormat::ADAT : MAudioClockDigitalFormat::SPDIF,
        playbackFormat_ == MAudioDigitalFormat::ADAT
            ? MAudioClockDigitalFormat::ADAT : MAudioClockDigitalFormat::SPDIF,
        /*lockSettings=*/false);

    Protocols::AVC::FCPFrame command{};
    command.length = frame.size();
    std::memcpy(command.data.data(), frame.data(), frame.size());

    ASFW_LOG(Audio,
             "[BeBoB] %{public}s: setting clock source=internal digIn=%u digOut=%u",
             DeviceName(), static_cast<unsigned>(captureFormat_),
             static_cast<unsigned>(playbackFormat_));

    const auto handle = fcpTransport_->SubmitCommand(
        command,
        [this, completion = std::move(completion)](
            Protocols::AVC::FCPStatus status,
            const Protocols::AVC::FCPFrame& response) mutable {
            if (status != Protocols::AVC::FCPStatus::kOk) {
                ASFW_LOG_ERROR(Audio,
                               "[BeBoB] clock command failed status=%u — device will "
                               "not be clocked and will not transmit",
                               static_cast<unsigned>(status));
                completion(kIOReturnIOError);
                return;
            }
            // Linux treats an AV/C refusal here as fatal to discovery; so do we.
            // A device that rejected its clock configuration is not one to
            // publish as an audio endpoint.
            const uint8_t responseCode = response.length > 0 ? response.data[0] : 0xFFU;
            if (responseCode != 0x09U && responseCode != 0x0CU) {
                ASFW_LOG_ERROR(Audio,
                               "[BeBoB] clock command refused: response=0x%02x",
                               responseCode);
                completion(kIOReturnUnsupported);
                return;
            }

            // Linux returns straight from here with no wait. The settle is kept
            // because the device is being reconfigured and this runs at install
            // time, not inside the stream-start budget; the vendor's 300 ms
            // clock-to-selector interlock is gone with the selector it separated.
            const uint64_t epoch = ++signalFormatEpoch_;
            auto finish = [this, epoch,
                           completion = std::move(completion)]() mutable {
                if (signalFormatEpoch_ != epoch) return;
                ASFW_LOG(Audio, "[BeBoB] %{public}s: clock settle complete",
                         DeviceName());
                // The vendor's blank-slate pass stops here, but its
                // non-blank-slate pass follows with
                // FWSettingsLevels::SendToDevice. Without that the write-only
                // parameter window is never asserted at all.
                SendParameterBlock(std::move(completion));
            };

            if (!timerScheduler_) {
                finish();
                return;
            }

            // Token deliberately dropped: the epoch makes a late firing inert,
            // and teardown bumps it.
            (void)timerScheduler_->ScheduleAfter(
                static_cast<uint64_t>(kMAudioClockSettleMs) * 1000ULL * 1000ULL,
                std::move(finish));
        });

    // No failure branch on the handle. SubmitCommand invokes the completion on
    // every path that returns an invalid handle — bad payload, no lock,
    // shutting down, refused by the command filter — so calling it here would
    // both use a moved-from std::function and complete the install twice.
    (void)handle;
}

void MAudioSpecialProtocol::ConfirmDuplexStart(ConfirmCallback callback) {
    // The inverted start choreography (H8).
    //
    // Every other BeBoB device here is fully configured before CMP: signal
    // format, then settle, then connect, then run. This firmware needs the rate
    // asserted *again* once host DMA is already running, and Linux is explicit
    // that this is what makes it transmit rather than a defensive re-send:
    //
    //   "The firmware customized by M-Audio uses these commands to start
    //    transmitting stream. This is not usual way."
    //     bebob_stream.c:648-659, after amdtp_domain_start()
    //
    // The coordinator has started host transmit before this stage runs
    // (kStartingHostTransmit precedes ConfirmingDeviceStart), so this is the
    // earliest point that matches Linux's ordering without adding a stage.
    //
    // Base confirm runs first: re-sending the rate into a connection that never
    // came up would produce a misleading AV/C failure for what is really a CMP
    // problem.
    BeBoBProtocol::ConfirmDuplexStart(
        [this, callback = std::move(callback)](IOReturn status,
                                               DuplexConfirmResult result) mutable {
            if (status != kIOReturnSuccess) {
                callback(status, result);
                return;
            }

            const AudioClockConfig clock{.sampleRateHz = currentRateHz_};
            ASFW_LOG(Audio,
                     "[BeBoB] %{public}s: re-sending signal format after DMA start "
                     "(rate=%u, interlock=%ums)",
                     DeviceName(), currentRateHz_, SignalFormatInterlockMs());

            ProgramSignalFormat(clock, [callback = std::move(callback), result](
                                           IOReturn fmtStatus) mutable {
                if (fmtStatus != kIOReturnSuccess) {
                    ASFW_LOG_ERROR(Audio,
                                   "[BeBoB] post-start signal format failed: 0x%08x — "
                                   "device will stay silent",
                                   fmtStatus);
                }
                callback(fmtStatus, result);
            });
        });
}

void MAudioSpecialProtocol::ReadClockHealth(HealthCallback callback) {
    // Deliberately does not interrogate the device. The one command that could
    // report a rate is the signal-format probe, and issuing FCP from a health
    // poll would put traffic on a freeze-prone device on a timer. Report what
    // we believe and let the caller see it is belief, not readback.
    callback(kIOReturnSuccess,
             DuplexHealthResult{.generation = busInfo_.GetGeneration(),
                                .appliedClock = appliedClock_,
                                .runtimeCaps = CapsForCurrentFormation(),
                                .sourceLocked = inputConnected_ && outputConnected_,
                                .clockReferenceHealthy = true,
                                .nominalRateHz = currentRateHz_});
}

void MAudioSpecialProtocol::SendLedState(bool illuminated) noexcept {
    if (!fcpTransport_) return;

    const auto frame = BuildMAudioLedCommand(illuminated);
    Protocols::AVC::FCPFrame command{};
    command.length = frame.size();
    std::memcpy(command.data.data(), frame.data(), frame.size());

    ASFW_LOG(Audio, "[MAudioControl] front-panel LED -> %{public}s",
             illuminated ? "on" : "off");
    // Fire and forget. A refused or lost LED write is cosmetic, and the next
    // press re-sends it; failing a meter poll over a lamp would be worse than
    // the lamp being wrong.
    (void)fcpTransport_->SubmitCommand(
        command, [](Protocols::AVC::FCPStatus status, const Protocols::AVC::FCPFrame&) {
            if (status != Protocols::AVC::FCPStatus::kOk) {
                ASFW_LOG_ERROR(Audio, "[MAudioControl] LED write failed status=%u",
                               static_cast<unsigned>(status));
            }
        });
}

void MAudioSpecialProtocol::ApplyRotaryDetents(
    const MAudio1814RotaryDelta& deltas) noexcept {
    if (!parameterLock_) return;

    // Byte 1 drives headphone channels 1/2, byte 2 drives 3/4. Both channels of
    // a pair share one quadlet, so a knob is one transaction however many
    // channels it moves. The third knob is assignable in the vendor Panel and we
    // have no reading of its assignment mask, so it is deliberately not applied.
    struct Binding {
        size_t rotaryIndex;
        uint32_t firstChannel;
    };
    constexpr Binding kBindings[] = {
        {static_cast<size_t>(MAudio1814Rotary::Headphone12), 0},
        {static_cast<size_t>(MAudio1814Rotary::Headphone34), 2},
    };

    IOLockLock(parameterLock_);
    for (const auto& binding : kBindings) {
        const int32_t detents = deltas.detents[binding.rotaryIndex];
        if (detents == 0) continue;

        for (uint32_t offset = 0; offset < 2; ++offset) {
            const uint32_t id = MakeMAudio1814ControlId(
                MAudio1814ControlGroup::HeadphoneVolume, binding.firstChannel + offset);
            int32_t level = desiredParameterImage_.ControlValue(id) +
                            detents * MAudioSpecialMeterState::kRotaryStep;
            if (level > MAudioSpecialParameterImage::kLevelMax) {
                level = MAudioSpecialParameterImage::kLevelMax;
            } else if (level < MAudioSpecialParameterImage::kLevelMin) {
                level = MAudioSpecialParameterImage::kLevelMin;
            }
            size_t changedIndex = 0;
            // Apply to desired state immediately so a physical detent is never
            // lost while a write is in flight. The confirmed image remains
            // untouched until FlushPendingParameterWrites receives an ACK.
            if (desiredParameterImage_.Apply(id, level, changedIndex)) {
                pendingParameterQuadlets_ |= (1ULL << changedIndex);
            }
        }
        ASFW_LOG(Audio, "[MAudioControl] knob %zu moved %d detent(s) -> headphone %u/%u",
                 binding.rotaryIndex, detents, binding.firstChannel + 1,
                 binding.firstChannel + 2);
    }
    IOLockUnlock(parameterLock_);

    FlushPendingParameterWrites();
}

void MAudioSpecialProtocol::FlushPendingParameterWrites() noexcept {
    if (!parameterLock_) return;

    size_t index = 0;
    uint32_t value = 0;
    IOLockLock(parameterLock_);
    if (parameterWriteInFlight_ || pendingParameterQuadlets_ == 0) {
        IOLockUnlock(parameterLock_);
        return;
    }
    // Lowest set bit first; order within the window does not matter because each
    // quadlet is independent.
    while ((pendingParameterQuadlets_ & (1ULL << index)) == 0) ++index;
    pendingParameterQuadlets_ &= ~(1ULL << index);
    value = desiredParameterImage_.QuadletAt(index);
    parameterWriteInFlight_ = true;
    IOLockUnlock(parameterLock_);

    SendParameterQuadlet(index, value, [this, index, value](IOReturn status) {
        if (parameterLock_) {
            IOLockLock(parameterLock_);
            parameterWriteInFlight_ = false;
            if (status == kIOReturnSuccess) {
                if (confirmedParameterImage_.QuadletAt(index) != value) {
                    (void)confirmedParameterImage_.SetQuadlet(index, value);
                    ++parameterRevision_;
                }
                // A newer detent may have changed the same quadlet while this
                // request was on the wire. Keep that newer value queued.
                if (desiredParameterImage_.QuadletAt(index) != value) {
                    pendingParameterQuadlets_ |= (1ULL << index);
                }
            } else {
                // Re-arm rather than drop: desired state is intentionally kept
                // separate from confirmed belief, and the poller is the bounded
                // retry path for physical controls.
                pendingParameterQuadlets_ |= (1ULL << index);
            }
            IOLockUnlock(parameterLock_);
        }
        if (status != kIOReturnSuccess) {
            ASFW_LOG_ERROR(Audio,
                           "[MAudioControl] knob write failed offset=0x%02zx kr=0x%08x",
                           index * sizeof(uint32_t), status);
            return; // Next meter poll retries; do not spin on a dead node.
        }
        FlushPendingParameterWrites();
    });
}

void MAudioSpecialProtocol::SendParameterBlock(
    std::function<void(IOReturn)> completion) {
    // The device cannot read this window. The cache begins with the lab-derived
    // audible routing defaults, and subsequent semantic writes amend it only
    // after an async acknowledgment. Reassert that belief at initialization;
    // never regenerate defaults here, or a later initialization would erase
    // a control change that the UI has already confirmed.
    // The image is held in wire order, so this is a straight copy. Copy under
    // the lock rather than handing the async write a span into the live image.
    std::array<uint8_t, MAudioSpecialParameterImage::kImageBytes> payload{};
    if (parameterLock_) {
        IOLockLock(parameterLock_);
        const auto bytes = desiredParameterImage_.Bytes();
        std::memcpy(payload.data(), bytes.data(), payload.size());
        // This write carries every quadlet, so anything a knob queued is now
        // covered and must not be re-sent behind it.
        pendingParameterQuadlets_ = 0;
        IOLockUnlock(parameterLock_);
    }

    const auto operationalNode = Discovery::TryOperationalNodeId(route_.nodeId);
    if (!operationalNode) {
        ASFW_LOG_ERROR(Audio,
                       "[BeBoB] parameter window skipped: node 0x%04x is not "
                       "operational",
                       static_cast<unsigned>(route_.nodeId));
        completion(kIOReturnSuccess);
        return;
    }

    const Async::FWAddress address{Async::FWAddress::QualifiedAddressParts{
        .addressHi = kMAudioParamAddressHi,
        .addressLo = kMAudioParamAddressLo,
        .nodeID = route_.nodeId}};

    ASFW_LOG(Audio,
             "[BeBoB] %{public}s: asserting parameter window +0x00..0x9c "
             "(%zu quadlets) node=0x%04x gen=%u",
             DeviceName(), MAudioSpecialParameterImage::kQuadletCount,
             static_cast<unsigned>(route_.nodeId),
             static_cast<unsigned>(route_.generation.value));

    (void)busOps_.WriteBlock(
        route_.generation, FW::NodeId{*operationalNode}, address, payload,
        // A control write, not a stream: the slowest universally supported
        // speed carries 160 bytes without depending on a negotiated rate.
        FW::FwSpeed::S100,
        [this, completion = std::move(completion)](
            Async::AsyncStatus status, std::span<const uint8_t>) mutable {
            if (status != Async::AsyncStatus::kSuccess) {
                // Not fatal to publication: the device still streams, it just
                // keeps whatever mixer state it powered up with.
                ASFW_LOG_ERROR(Audio,
                               "[BeBoB] parameter window write failed "
                               "status=%u — mixer state left indeterminate",
                               static_cast<unsigned>(status));
            } else {
                ASFW_LOG(Audio, "[BeBoB] parameter window asserted");
            }
            completion(kIOReturnSuccess);
        });
}

void MAudioSpecialProtocol::SendParameterQuadlet(
    size_t index, uint32_t value, IAudioControlSurface::ApplyCallback completion) {
    if (index >= MAudioSpecialParameterImage::kQuadletCount) {
        completion(kIOReturnBadArgument);
        return;
    }
    const auto operationalNode = Discovery::TryOperationalNodeId(route_.nodeId);
    if (!operationalNode) {
        ASFW_LOG_ERROR(Audio,
                       "[MAudioControl] parameter write skipped: node 0x%04x is not operational",
                       static_cast<unsigned>(route_.nodeId));
        completion(kIOReturnNoDevice);
        return;
    }

    const Async::FWAddress address{Async::FWAddress::QualifiedAddressParts{
        .addressHi = kMAudioParamAddressHi,
        .addressLo = kMAudioParamAddressLo + static_cast<uint32_t>(index * sizeof(uint32_t)),
        .nodeID = route_.nodeId}};
    const std::array<uint8_t, 4> payload{
        static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};

    (void)busOps_.WriteBlock(
        route_.generation, FW::NodeId{*operationalNode}, address, payload, FW::FwSpeed::S100,
        [completion = std::move(completion)](Async::AsyncStatus status,
                                             std::span<const uint8_t>) mutable {
            completion(status == Async::AsyncStatus::kSuccess ? kIOReturnSuccess : kIOReturnError);
        });
}

} // namespace ASFW::Audio::BeBoB
