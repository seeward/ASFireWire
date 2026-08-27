// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024 ASFireWire Project
//
// SPro24DspProtocol.cpp - Focusrite Saffire Pro 24 DSP protocol implementation

#include "SPro24DspProtocol.hpp"
#include "SPro24DspRouting.hpp"
#include "../Core/DICENotificationMailbox.hpp"
#include "../../../../Common/CallbackUtils.hpp"
#include "../../../../Logging/Logging.hpp"
#include "../../../../Scheduling/ITimerScheduler.hpp"
#include <DriverKit/IOLib.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>

namespace ASFW::Audio::DICE::Focusrite {

namespace {

// MixControl's CPro24DSPUserDevice::SetState does not merely cache this
// application section. In FX mode it writes the current rate's derived DSP
// coefficients and then sends the notices below. A DICE stream can be valid
// while the DSP has not latched that state, resulting in an all-zero device TX
// payload. These offsets are application-section relative, expressed within
// an active-rate bank from the Saffire Pro 24 DSP layout. They are
// cross-validated with the local snd-firewire-ctl-services spro24dsp.rs and
// the vendor MixControl writer.
struct FxWriteFragment {
    uint16_t offset;
    uint8_t quadlets;
};

constexpr uint32_t kFxBank44k1Offset = 0x120;
constexpr uint32_t kFxBank48kOffset = 0x230;
constexpr uint32_t kFxBank88k2Offset = 0x340;
constexpr uint32_t kFxBank96kOffset = 0x450;
constexpr uint64_t kExtensionCommandPollDelayNs = 50ULL * 1000ULL * 1000ULL;
constexpr uint32_t kExtensionCommandPollLimit = 10;
constexpr uint64_t kRouterStreamConfigNoticePollDelayNs = 10ULL * 1000ULL * 1000ULL;
constexpr uint32_t kRouterStreamConfigNoticePollLimit = 100;
constexpr uint32_t kRouterStreamConfigNoticeMask =
    ::ASFW::Audio::DICE::Notify::kRxConfigChange |
    ::ASFW::Audio::DICE::Notify::kTxConfigChange;

[[nodiscard]] const DiceRouterEntry* FindRoute(const DiceRouterEntries& routes,
                                                uint8_t destinationBlock,
                                                uint8_t destinationChannel) noexcept {
    for (uint16_t index = 0; index < routes.count; ++index) {
        const auto& route = routes.At(index);
        if (route.destinationBlock == destinationBlock &&
            route.destinationChannel == destinationChannel) {
            return &route;
        }
    }
    return nullptr;
}

/// Translate only complete, verified stereo pair routes into an app-facing
/// identity. A mixed or unknown pair stays Unknown rather than exposing a
/// TCAT address or inventing a source name.
[[nodiscard]] SPro24DspControl::OutputRouteSource OutputRouteSourceForPair(
    const DiceRouterEntries& routes, uint8_t pair) noexcept {
    constexpr uint8_t kPhysicalOutputBlock = 4; // Ins0 destination block.
    const uint8_t leftChannel = static_cast<uint8_t>(pair * 2U);
    const auto* left = FindRoute(routes, kPhysicalOutputBlock, leftChannel);
    const auto* right = FindRoute(routes, kPhysicalOutputBlock,
                                  static_cast<uint8_t>(leftChannel + 1U));
    if (!left || !right || left->sourceBlock != right->sourceBlock ||
        right->sourceChannel != static_cast<uint8_t>(left->sourceChannel + 1U) ||
        (left->sourceChannel & 1U) != 0U) {
        return SPro24DspControl::OutputRouteSource::Unknown;
    }

    if (left->sourceBlock == 11) { // AVS0: host playback.
        switch (left->sourceChannel / 2U) {
        case 0: return SPro24DspControl::OutputRouteSource::HostPlayback12;
        case 1: return SPro24DspControl::OutputRouteSource::HostPlayback34;
        case 2: return SPro24DspControl::OutputRouteSource::HostPlayback56;
        case 3: return SPro24DspControl::OutputRouteSource::HostPlayback78;
        default: return SPro24DspControl::OutputRouteSource::Unknown;
        }
    }
    if (left->sourceBlock == 2) { // TCAT mixer rows.
        switch (left->sourceChannel / 2U) {
        case 0: return SPro24DspControl::OutputRouteSource::Mixer12;
        case 1: return SPro24DspControl::OutputRouteSource::Mixer34;
        case 2: return SPro24DspControl::OutputRouteSource::Mixer56;
        case 3: return SPro24DspControl::OutputRouteSource::Mixer78;
        default: return SPro24DspControl::OutputRouteSource::Unknown;
        }
    }
    if (left->sourceBlock == 4 && left->sourceChannel == 0) {
        return SPro24DspControl::OutputRouteSource::Analog12;
    }
    if (left->sourceBlock == 0 && left->sourceChannel == 6) {
        return SPro24DspControl::OutputRouteSource::Spdif12;
    }
    return SPro24DspControl::OutputRouteSource::Unknown;
}

// The rate mode whose CURRENT_CONFIG router image the semantic surface is
// built from. SPro24 publishes 44.1/48 kHz today and both live in the low
// router block. The vendor signal table is rate-scoped, so publishing the 2x
// modes is a change to this constant and the reads it feeds, not to the
// projection: the two DSP return pairs then resolve at their 2x channels.
constexpr DiceRateMode kSemanticRouterRateMode = DiceRateMode::Low;

[[nodiscard]] constexpr uint32_t ExtensionRateFlag(DiceRateMode mode) noexcept {
    switch (mode) {
    case DiceRateMode::Low: return ExtensionCommandOpcode::kRateLow;
    case DiceRateMode::Middle: return ExtensionCommandOpcode::kRateMiddle;
    case DiceRateMode::High: return ExtensionCommandOpcode::kRateHigh;
    }
    return 0;
}

constexpr std::array<FxWriteFragment, 10> kEqFragments{{
    {0x018, 2}, {0x020, 5}, {0x034, 5}, {0x048, 5}, {0x05c, 5},
    {0x0a0, 2}, {0x0a8, 5}, {0x0bc, 5}, {0x0d0, 5}, {0x0e4, 5},
}};
constexpr std::array<FxWriteFragment, 2> kCompressorFragments{{
    {0x000, 6}, {0x088, 6},
}};
constexpr std::array<FxWriteFragment, 2> kReverbFragments{{
    {0x070, 6}, {0x0f8, 6},
}};

constexpr std::array<SwNotice, 5> kEqNotices{{
    SwNotice::EqOutputAll,
    SwNotice::EqLowAll,
    SwNotice::EqLowMidAll,
    SwNotice::EqHighMidAll,
    SwNotice::EqHighAll,
}};

constexpr std::array<SwNotice, 1> kChannelStripNotice{{SwNotice::ChStripFlags}};
constexpr std::array<SwNotice, 1> kCompressorNotice{{SwNotice::CompressorAll}};
constexpr std::array<SwNotice, 1> kReverbNotice{{SwNotice::Reverb}};

[[nodiscard]] constexpr bool IsInputControl(uint32_t controlId) noexcept {
    return controlId >= SPro24DspControl::kMicInputMode1 &&
           controlId <= SPro24DspControl::kLineInputLevel56;
}

[[nodiscard]] constexpr bool IsOutputControl(uint32_t controlId) noexcept {
    return (controlId >= SPro24DspControl::kOutputVolumeFirst &&
            controlId < SPro24DspControl::kOutputVolumeFirst + 6U) ||
           (controlId >= SPro24DspControl::kOutputMuteFirst &&
            controlId < SPro24DspControl::kOutputMuteFirst + 6U) ||
           controlId == SPro24DspControl::kGlobalMute ||
           controlId == SPro24DspControl::kGlobalDim;
}

[[nodiscard]] bool ApplyInputControl(InputParams& input,
                                     uint32_t controlId,
                                     int32_t value) noexcept {
    switch (controlId) {
    case SPro24DspControl::kMicInputMode1:
    case SPro24DspControl::kMicInputMode2:
        if (value < static_cast<int32_t>(MicInputLevel::Line) ||
            value > static_cast<int32_t>(MicInputLevel::Instrument)) return false;
        input.micLevels[controlId - SPro24DspControl::kMicInputMode1] =
            static_cast<MicInputLevel>(value);
        return true;
    case SPro24DspControl::kLineInputLevel34:
    case SPro24DspControl::kLineInputLevel56:
        if (value < static_cast<int32_t>(LineInputLevel::Low) ||
            value > static_cast<int32_t>(LineInputLevel::High)) return false;
        input.lineLevels[controlId - SPro24DspControl::kLineInputLevel34] =
            static_cast<LineInputLevel>(value);
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool InputControlMatches(const InputParams& input,
                                        uint32_t controlId,
                                        int32_t value) noexcept {
    switch (controlId) {
    case SPro24DspControl::kMicInputMode1:
    case SPro24DspControl::kMicInputMode2:
        return static_cast<int32_t>(
                   input.micLevels[controlId - SPro24DspControl::kMicInputMode1]) == value;
    case SPro24DspControl::kLineInputLevel34:
    case SPro24DspControl::kLineInputLevel56:
        return static_cast<int32_t>(
                   input.lineLevels[controlId - SPro24DspControl::kLineInputLevel34]) == value;
    default:
        return false;
    }
}

[[nodiscard]] bool ApplyOutputControl(OutputGroupState& output,
                                      uint32_t controlId,
                                      int32_t value) noexcept {
    if (controlId >= SPro24DspControl::kOutputVolumeFirst &&
        controlId < SPro24DspControl::kOutputVolumeFirst + output.volumes.size()) {
        if (value < OutputGroupState::kVolMin || value > OutputGroupState::kVolMax) return false;
        output.volumes[controlId - SPro24DspControl::kOutputVolumeFirst] =
            static_cast<int8_t>(value);
        return true;
    }
    if (controlId >= SPro24DspControl::kOutputMuteFirst &&
        controlId < SPro24DspControl::kOutputMuteFirst + output.volMutes.size()) {
        if (value != 0 && value != 1) return false;
        output.volMutes[controlId - SPro24DspControl::kOutputMuteFirst] = value != 0;
        return true;
    }
    if (value != 0 && value != 1) return false;
    if (controlId == SPro24DspControl::kGlobalMute) {
        output.muteEnabled = value != 0;
        return true;
    }
    if (controlId == SPro24DspControl::kGlobalDim) {
        output.dimEnabled = value != 0;
        return true;
    }
    return false;
}

[[nodiscard]] bool OutputControlMatches(const OutputGroupState& output,
                                         uint32_t controlId,
                                         int32_t value) noexcept {
    if (controlId >= SPro24DspControl::kOutputVolumeFirst &&
        controlId < SPro24DspControl::kOutputVolumeFirst + output.volumes.size()) {
        return output.volumes[controlId - SPro24DspControl::kOutputVolumeFirst] == value;
    }
    if (controlId >= SPro24DspControl::kOutputMuteFirst &&
        controlId < SPro24DspControl::kOutputMuteFirst + output.volMutes.size()) {
        return static_cast<int32_t>(
                   output.volMutes[controlId - SPro24DspControl::kOutputMuteFirst]) == value;
    }
    if (controlId == SPro24DspControl::kGlobalMute) {
        return static_cast<int32_t>(output.muteEnabled) == value;
    }
    if (controlId == SPro24DspControl::kGlobalDim) {
        return static_cast<int32_t>(output.dimEnabled) == value;
    }
    return false;
}

[[nodiscard]] bool HasUsableGenericStreamImage(
    const Audio::AudioStreamRuntimeCaps& caps) noexcept {
    return caps.sampleRateHz != 0 &&
           caps.deviceToHostStreamCount != 0 &&
           caps.hostToDeviceStreamCount != 0 &&
           caps.hostInputPcmChannels != 0 &&
           caps.hostOutputPcmChannels != 0 &&
           caps.deviceToHostAm824Slots != 0 &&
           caps.hostToDeviceAm824Slots != 0;
}

bool FxBankOffsetForRate(uint32_t rateHz, uint32_t& outOffset) noexcept {
    switch (rateHz) {
    case 44100: outOffset = kFxBank44k1Offset; return true;
    case 48000: outOffset = kFxBank48kOffset; return true;
    case 88200: outOffset = kFxBank88k2Offset; return true;
    case 96000: outOffset = kFxBank96kOffset; return true;
    default: return false;
    }
}

} // anonymous namespace

// ============================================================================
// SPro24DspProtocol Implementation
// ============================================================================

SPro24DspProtocol::SPro24DspProtocol(Protocols::Ports::FireWireBusOps& busOps,
                                     Protocols::Ports::FireWireBusInfo& busInfo,
                                     Discovery::DeviceRegistry& routeRegistry,
                                     const Discovery::DeviceRouteToken& route,
                                     IRM::IRMClient* irmClient,
                                     Scheduling::ITimerScheduler* timerScheduler)
    : tcat_(busOps,
            busInfo,
            routeRegistry,
            route,
            irmClient,
            timerScheduler,
            TCAT::DICETcatRuntimePolicy{
                // Prefer the TCAT rate-mode description when populated;
                // generic DICE discovery owns cold 0/-1 count refinement.
                .preferExtensionStreamGeometry = true,
            })
    , timerScheduler_(timerScheduler)
{
    tcat_.SetStoppedPrepareHook(
        [this](const AudioClockConfig& clock, VoidCallback callback) {
            PrepareStoppedForRate(clock, std::move(callback));
        });
    semanticMatrixLock_ = IOLockAlloc();
    semanticControlLock_ = IOLockAlloc();
    ASFW_LOG(DICE, "SPro24DspProtocol created for instance=%llu node=0x%04x",
             route.deviceInstanceId.value,
             route.nodeId);
}

SPro24DspProtocol::~SPro24DspProtocol() {
    CancelExtensionCommandPoll();
    if (semanticMatrixLock_) {
        IOLockFree(semanticMatrixLock_);
        semanticMatrixLock_ = nullptr;
    }
    if (semanticControlLock_) {
        IOLockFree(semanticControlLock_);
        semanticControlLock_ = nullptr;
    }
}

bool SPro24DspProtocol::GetRuntimeAudioStreamCaps(AudioStreamRuntimeCaps& outCaps) const {
    return tcat_.GetRuntimeAudioStreamCaps(outCaps);
}

IOReturn SPro24DspProtocol::Initialize() {
    InitializeAsync([](IOReturn status) {
        if (status != kIOReturnSuccess) {
            ASFW_LOG(DICE, "SPro24DspProtocol initialization failed: 0x%x", status);
        }
    });
    return kIOReturnSuccess;
}

void SPro24DspProtocol::InitializeAsync(InitCallback callback) {
    ASFW_LOG(DICE, "SPro24DspProtocol::InitializeAsync defers generic DICE discovery to TCAT runtime");
    const IOReturn status = tcat_.Initialize();
    callback(status);
    if (status == kIOReturnSuccess) {
        PrimeSemanticMatrix();
        PrimeSemanticControls();
    }
}

bool SPro24DspProtocol::CopyAudioSemanticMatrix(
    Audio::AudioSemanticMatrixSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    if (!semanticMatrixLock_) return false;
    IOLockLock(semanticMatrixLock_);
    const bool ready = semanticMatrixReady_ &&
        BuildSPro24DspSemanticMatrix(semanticMixerCoefficients_, semanticRouterEntries_,
                                     semanticRateMode_, outSnapshot);
    if (ready) {
        outSnapshot.stateRevision = semanticMatrixRevision_;
        semanticStripStates_.CopyInto(outSnapshot);
    }
    IOLockUnlock(semanticMatrixLock_);
    return ready;
}

void SPro24DspProtocol::ApplyAudioSemanticMatrixCrosspoint(
    uint32_t outputPortId, uint32_t inputPortId, uint16_t coefficient,
    Audio::IAudioSemanticMatrix::ApplyCallback callback) {
    // A scalar TCAT cell is not a MixControl monitor-strip control. The
    // current image does not establish the bus grouping, link state, pan law,
    // or physical-output route, so accepting this selector made the UI write
    // a real coefficient with a false audible meaning. Keep the low-level
    // primitive private until a grouped transaction is independently verified.
    (void)outputPortId;
    (void)inputPortId;
    (void)coefficient;
    if (callback) callback(kIOReturnUnsupported);
}

void SPro24DspProtocol::FinishSemanticMatrixWrite(
    IOReturn status, Audio::IAudioSemanticMatrix::ApplyCallback callback) noexcept {
    if (semanticMatrixLock_) {
        IOLockLock(semanticMatrixLock_);
        semanticMatrixWriteInFlight_ = false;
        IOLockUnlock(semanticMatrixLock_);
    }
    if (callback) callback(status);
}

void SPro24DspProtocol::ReconcileSemanticStripStates() noexcept {
    // Caller holds semanticMatrixLock_. Mute and solo are the driver's own
    // policy, not device state, so anything that writes a coefficient behind
    // our back -- the vendor application, MCP, another host -- can leave a
    // record describing a strip that is no longer silent. Hardware wins: a
    // stale record would otherwise "restore" a level the user never set.
    if (semanticStripStates_.Count() == 0) return;
    Audio::AudioSemanticMatrixSnapshot snapshot{};
    if (!BuildSPro24DspSemanticMatrix(semanticMixerCoefficients_, semanticRouterEntries_,
                                      semanticRateMode_, snapshot)) {
        semanticStripStates_.Clear();
        return;
    }
    semanticStripStates_.CopyInto(snapshot);

    std::array<std::pair<uint32_t, uint32_t>,
               Audio::kMaxAudioSemanticMatrixStripStates> stale{};
    uint32_t staleCount = 0;
    for (uint32_t index = 0; index < snapshot.stripStateCount; ++index) {
        const auto& record = snapshot.stripStates[index];
        Audio::AudioSemanticMatrixStripCells strip{};
        const bool resolved = Audio::ResolveAudioSemanticMatrixStrip(
            snapshot, record.outputPresentationGroupId,
            record.inputPresentationGroupId, strip);
        bool drop = !resolved;
        if (resolved && snapshot.StripIsSuppressed(record.outputPresentationGroupId,
                                                   record.inputPresentationGroupId)) {
            drop = snapshot.Coefficient(strip.outputLeft, strip.inputLeft) != 0 ||
                   snapshot.Coefficient(strip.outputRight, strip.inputRight) != 0;
        }
        if (drop) {
            stale[staleCount++] = {record.outputPresentationGroupId,
                                   record.inputPresentationGroupId};
        }
    }
    for (uint32_t index = 0; index < staleCount; ++index) {
        semanticStripStates_.Remove(stale[index].first, stale[index].second);
    }
    semanticStripStates_.Prune();
}

void SPro24DspProtocol::BeginSemanticMixerGesture(
    SemanticMixerGestureStage stage,
    Audio::IAudioSemanticMatrix::ApplyCallback callback) {
    if (!semanticMatrixLock_) {
        if (callback) callback(kIOReturnNotReady);
        return;
    }
    IOLockLock(semanticMatrixLock_);
    const bool ready = semanticMatrixReady_ && !semanticMatrixWriteInFlight_;
    if (ready) semanticMatrixWriteInFlight_ = true;
    const auto rateMode = semanticRateMode_;
    IOLockUnlock(semanticMatrixLock_);
    if (!ready) {
        if (callback) callback(kIOReturnBusy);
        return;
    }

    EnsureExtensionsLoaded(
        [this, rateMode, stage = std::move(stage),
         callback = std::move(callback)](IOReturn sectionsStatus) mutable {
            if (sectionsStatus != kIOReturnSuccess) {
                FinishSemanticMatrixWrite(sectionsStatus, std::move(callback));
                return;
            }
            tcat_.Transaction().ReadExtensionCaps(
                extensionSections_,
                [this, rateMode, stage = std::move(stage),
                 callback = std::move(callback)](IOReturn capsStatus,
                                                 DiceExtensionCaps caps) mutable {
                    if (capsStatus != kIOReturnSuccess) {
                        FinishSemanticMatrixWrite(capsStatus, std::move(callback));
                        return;
                    }
                    // The router is re-read with the coefficients so that an
                    // external route change cannot let a stale group ID resolve
                    // against a source that is no longer there.
                    tcat_.Transaction().ReadCurrentConfigRouterEntries(
                        extensionSections_, caps, kSemanticRouterRateMode,
                        [this, caps, rateMode, stage = std::move(stage),
                         callback = std::move(callback)](
                            IOReturn routeStatus, DiceRouterEntries routes) mutable {
                            if (routeStatus != kIOReturnSuccess) {
                                FinishSemanticMatrixWrite(routeStatus, std::move(callback));
                                return;
                            }
                            tcat_.Transaction().ReadMixerCoefficients(
                                extensionSections_, caps,
                                [this, caps, routes, rateMode, stage = std::move(stage),
                                 callback = std::move(callback)](
                                    IOReturn readStatus,
                                    DiceMixerCoefficients current) mutable {
                                    if (readStatus != kIOReturnSuccess) {
                                        FinishSemanticMatrixWrite(readStatus,
                                                                  std::move(callback));
                                        return;
                                    }
                                    Audio::AudioSemanticMatrixSnapshot snapshot{};
                                    if (!BuildSPro24DspSemanticMatrix(current, routes,
                                                                      rateMode, snapshot)) {
                                        FinishSemanticMatrixWrite(kIOReturnBadArgument,
                                                                  std::move(callback));
                                        return;
                                    }
                                    IOLockLock(semanticMatrixLock_);
                                    semanticStripStates_.CopyInto(snapshot);
                                    IOLockUnlock(semanticMatrixLock_);
                                    stage(caps, routes, current, snapshot,
                                          std::move(callback));
                                });
                        });
                });
        });
}

void SPro24DspProtocol::WriteNextSemanticMixerCell(
    DiceExtensionCaps caps, PendingSemanticMixerCellWrites pending,
    std::function<void(IOReturn)> completion) {
    if (pending.next >= pending.count) {
        completion(kIOReturnSuccess);
        return;
    }
    const auto cell = pending.cells[pending.next];
    tcat_.Transaction().WriteMixerCoefficient(
        extensionSections_, caps, cell.output, cell.input, cell.value,
        [this, caps, pending, completion = std::move(completion)](
            IOReturn status) mutable {
            if (status != kIOReturnSuccess) {
                completion(status);
                return;
            }
            ++pending.next;
            WriteNextSemanticMixerCell(caps, pending, std::move(completion));
        });
}

void SPro24DspProtocol::CommitSemanticMixerCells(
    DiceExtensionCaps caps, DiceRouterEntries routes,
    PendingSemanticMixerCellWrites pending,
    Audio::AudioSemanticMatrixStripStateSet states,
    Audio::IAudioSemanticMatrix::ApplyCallback callback) {
    WriteNextSemanticMixerCell(
        caps, pending,
        [this, caps, routes, pending, states,
         callback = std::move(callback)](IOReturn writeStatus) mutable {
            if (writeStatus != kIOReturnSuccess) {
                FinishSemanticMatrixWrite(writeStatus, std::move(callback));
                return;
            }
            tcat_.Transaction().ReadMixerCoefficients(
                extensionSections_, caps,
                [this, caps, routes, pending, states, callback = std::move(callback)](
                    IOReturn readbackStatus, DiceMixerCoefficients readback) mutable {
                    bool confirmed = readbackStatus == kIOReturnSuccess;
                    // Every written cell is confirmed, not just the last one:
                    // a solo writes many, and a partially applied solo is a
                    // worse state to publish than a failed one.
                    for (uint32_t index = 0; confirmed && index < pending.count; ++index) {
                        const auto& cell = pending.cells[index];
                        confirmed = readback.At(cell.output, cell.input) == cell.value;
                    }
                    if (!confirmed) {
                        FinishSemanticMatrixWrite(
                            readbackStatus == kIOReturnSuccess ? kIOReturnError
                                                               : readbackStatus,
                            std::move(callback));
                        return;
                    }
                    states.Prune();
                    IOLockLock(semanticMatrixLock_);
                    semanticExtensionCaps_ = caps;
                    semanticRouterEntries_ = routes;
                    semanticMixerCoefficients_ = readback;
                    semanticStripStates_ = states;
                    semanticMatrixReady_ = true;
                    ++semanticMatrixRevision_;
                    IOLockUnlock(semanticMatrixLock_);
                    FinishSemanticMatrixWrite(kIOReturnSuccess, std::move(callback));
                });
        });
}

void SPro24DspProtocol::ApplyAudioSemanticMatrixStereoStrip(
    const Audio::IAudioSemanticMatrix::StereoStripRequest& request,
    Audio::IAudioSemanticMatrix::ApplyCallback callback) {
    BeginSemanticMixerGesture(
        [this, request](DiceExtensionCaps caps, DiceRouterEntries routes,
                        DiceMixerCoefficients current,
                        const Audio::AudioSemanticMatrixSnapshot& snapshot,
                        Audio::IAudioSemanticMatrix::ApplyCallback callback) mutable {
            const auto cells = ResolveSPro24DspStripCells(
                snapshot, request.outputPresentationGroupId,
                request.inputPresentationGroupId);
            // One source cell against two destination rows is a pan; two source
            // cells against their own rows is a balance. They are different
            // laws, and the strip's shape is what selects between them.
            const auto coefficients = cells
                ? (cells->mono
                       ? MakeSPro24DspMonoStripCoefficients(request.levelMilliDb,
                                                            request.balanceMilli)
                       : MakeSPro24DspStereoStripCoefficients(request.levelMilliDb,
                                                              request.balanceMilli))
                : std::nullopt;
            if (!cells || !coefficients) {
                FinishSemanticMatrixWrite(kIOReturnBadArgument, std::move(callback));
                return;
            }

            auto states = semanticStripStates_;
            const bool suppressed = states.IsSuppressed(request.outputPresentationGroupId,
                                                        request.inputPresentationGroupId);
            // A fader still moves while the strip is muted; the gesture updates
            // what the strip returns to rather than unmuting it behind the
            // user's back.
            if (suppressed || states.Find(request.outputPresentationGroupId,
                                          request.inputPresentationGroupId) != nullptr) {
                auto record = Audio::AudioSemanticMatrixStripState{
                    .outputPresentationGroupId = request.outputPresentationGroupId,
                    .inputPresentationGroupId = request.inputPresentationGroupId,
                    .nominalLeft = coefficients->left,
                    .nominalRight = coefficients->right,
                };
                if (const auto* existing = states.Find(request.outputPresentationGroupId,
                                                       request.inputPresentationGroupId)) {
                    record.muted = existing->muted;
                    record.soloed = existing->soloed;
                }
                if (!states.Upsert(record)) {
                    FinishSemanticMatrixWrite(kIOReturnNoResources, std::move(callback));
                    return;
                }
            }

            const uint16_t left = suppressed ? uint16_t{0} : coefficients->left;
            const uint16_t right = suppressed ? uint16_t{0} : coefficients->right;
            PendingSemanticMixerCellWrites pending{};
            pending.cells[pending.count++] = {.output = cells->outputLeft,
                                              .input = cells->inputLeft,
                                              .value = left};
            pending.cells[pending.count++] = {.output = cells->outputRight,
                                              .input = cells->inputRight,
                                              .value = right};
            (void)current;
            CommitSemanticMixerCells(caps, routes, pending, states, std::move(callback));
        },
        std::move(callback));
}

void SPro24DspProtocol::ApplyAudioSemanticMatrixStripSuppression(
    const Audio::IAudioSemanticMatrix::StripSuppressionRequest& request,
    Audio::IAudioSemanticMatrix::ApplyCallback callback) {
    BeginSemanticMixerGesture(
        [this, request](DiceExtensionCaps caps, DiceRouterEntries routes,
                        DiceMixerCoefficients current,
                        const Audio::AudioSemanticMatrixSnapshot& snapshot,
                        Audio::IAudioSemanticMatrix::ApplyCallback callback) mutable {
            const auto target = ResolveSPro24DspStripCells(
                snapshot, request.outputPresentationGroupId,
                request.inputPresentationGroupId);
            if (!target) {
                FinishSemanticMatrixWrite(kIOReturnBadArgument, std::move(callback));
                return;
            }

            // Solo is a property of the whole bus, so every strip on it can
            // change value even though the user gestured on one.
            std::array<Audio::AudioSemanticMatrixStripCells,
                       Audio::kMaxAudioSemanticMatrixStripsPerBus> strips{};
            uint32_t stripCount = 0;
            if (!Audio::EnumerateAudioSemanticMatrixBusStrips(
                    snapshot, request.outputPresentationGroupId, strips, stripCount)) {
                FinishSemanticMatrixWrite(kIOReturnBadArgument, std::move(callback));
                return;
            }

            auto states = semanticStripStates_;
            // Capture every strip's nominal before changing the policy. A strip
            // silenced by someone else's solo has a live coefficient of zero,
            // so once it is suppressed its own level is no longer recoverable
            // from the hardware.
            for (uint32_t index = 0; index < stripCount; ++index) {
                const auto& strip = strips[index];
                const auto nominal =
                    Audio::AudioSemanticMatrixStripNominal(snapshot, states, strip);
                auto record = Audio::AudioSemanticMatrixStripState{
                    .outputPresentationGroupId = strip.outputPresentationGroupId,
                    .inputPresentationGroupId = strip.inputPresentationGroupId,
                    .nominalLeft = nominal.left,
                    .nominalRight = nominal.right,
                };
                if (const auto* existing = states.Find(strip.outputPresentationGroupId,
                                                       strip.inputPresentationGroupId)) {
                    record.muted = existing->muted;
                    record.soloed = existing->soloed;
                }
                if (strip.inputPresentationGroupId == request.inputPresentationGroupId) {
                    record.muted = request.muted ? 1 : 0;
                    record.soloed = request.soloed ? 1 : 0;
                }
                if (!states.Upsert(record)) {
                    // Refusing is the only safe answer: a record that does not
                    // fit is a nominal level nothing could ever restore.
                    FinishSemanticMatrixWrite(kIOReturnNoResources, std::move(callback));
                    return;
                }
            }

            // Only cells whose effective value actually changes are written, so
            // an idempotent gesture costs no bus traffic.
            PendingSemanticMixerCellWrites pending{};
            for (uint32_t index = 0; index < stripCount; ++index) {
                const auto& strip = strips[index];
                const auto effective =
                    Audio::AudioSemanticMatrixStripEffective(snapshot, states, strip);
                const auto native = ResolveSPro24DspStripCells(
                    snapshot, strip.outputPresentationGroupId,
                    strip.inputPresentationGroupId);
                if (!native) continue;
                const SemanticMixerCellWrite wanted[2] = {
                    {.output = native->outputLeft, .input = native->inputLeft,
                     .value = effective.left},
                    {.output = native->outputRight, .input = native->inputRight,
                     .value = effective.right},
                };
                for (const auto& cell : wanted) {
                    if (current.At(cell.output, cell.input) == cell.value) continue;
                    if (pending.count >= kMaxSemanticMixerCellWrites) {
                        FinishSemanticMatrixWrite(kIOReturnNoResources,
                                                  std::move(callback));
                        return;
                    }
                    pending.cells[pending.count++] = cell;
                }
            }

            CommitSemanticMixerCells(caps, routes, pending, states, std::move(callback));
        },
        std::move(callback));
}

bool SPro24DspProtocol::CopyAudioControlSurfaceSnapshot(
    Audio::AudioControlSurfaceSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    if (!semanticControlLock_) return false;

    // The input/output control block does not contain the source selected for
    // a physical output. Snapshot the independently-owned active router first
    // and publish only its semantic pair identity below.
    DiceRouterEntries routes{};
    if (semanticMatrixLock_) {
        IOLockLock(semanticMatrixLock_);
        routes = semanticRouterEntries_;
        IOLockUnlock(semanticMatrixLock_);
    }

    IOLockLock(semanticControlLock_);
    const auto& state = semanticControls_;
    if (!state.valid) {
        IOLockUnlock(semanticControlLock_);
        return false;
    }

    outSnapshot.kind = Audio::AudioControlSurfaceKind::FocusriteSPro24Dsp;
    outSnapshot.stateRevision = state.revision;
    auto emit = [&outSnapshot](uint32_t id, int32_t value) {
        outSnapshot.values[outSnapshot.valueCount++] = {.id = id, .value = value};
    };
    emit(SPro24DspControl::kMicInputMode1, static_cast<int32_t>(state.input.micLevels[0]));
    emit(SPro24DspControl::kMicInputMode2, static_cast<int32_t>(state.input.micLevels[1]));
    emit(SPro24DspControl::kLineInputLevel34, static_cast<int32_t>(state.input.lineLevels[0]));
    emit(SPro24DspControl::kLineInputLevel56, static_cast<int32_t>(state.input.lineLevels[1]));
    for (uint32_t index = 0; index < state.output.volumes.size(); ++index) {
        emit(SPro24DspControl::kOutputVolumeFirst + index, state.output.volumes[index]);
        emit(SPro24DspControl::kOutputMuteFirst + index, state.output.volMutes[index] ? 1 : 0);
    }
    for (uint8_t pair = 0; pair < 3; ++pair) {
        emit(SPro24DspControl::kOutputRouteSourceFirst + pair,
             static_cast<int32_t>(OutputRouteSourceForPair(routes, pair)));
    }
    emit(SPro24DspControl::kGlobalMute, state.output.muteEnabled ? 1 : 0);
    emit(SPro24DspControl::kGlobalDim, state.output.dimEnabled ? 1 : 0);
    for (uint32_t index = 0; index < 2; ++index) {
        emit(SPro24DspControl::kChannelStripEqFirst + index, state.effects.eqEnable[index] ? 1 : 0);
        emit(SPro24DspControl::kChannelStripCompressorFirst + index,
             state.effects.compEnable[index] ? 1 : 0);
        emit(SPro24DspControl::kChannelStripEqAfterCompFirst + index,
             state.effects.eqAfterComp[index] ? 1 : 0);
    }
    emit(SPro24DspControl::kReverbEnabled, state.reverb.enabled ? 1 : 0);
    emit(SPro24DspControl::kInSituMode, state.inSitu ? 1 : 0);
    IOLockUnlock(semanticControlLock_);
    return true;
}

void SPro24DspProtocol::ApplyAudioControlValue(
    uint32_t controlId, int32_t value, Audio::IAudioControlSurface::ApplyCallback callback) {
    if (!callback) return;
    if (!IsInputControl(controlId) && !IsOutputControl(controlId)) {
        callback(kIOReturnUnsupported);
        return;
    }
    if (!BeginSemanticControlWrite(callback)) return;

    if (IsInputControl(controlId)) {
        GetInputParams([this, controlId, value, callback = std::move(callback)](
                           IOReturn readStatus, InputParams current) mutable {
            if (readStatus != kIOReturnSuccess) {
                FinishSemanticControlWrite(readStatus, std::move(callback));
                return;
            }
            if (!ApplyInputControl(current, controlId, value)) {
                FinishSemanticControlWrite(kIOReturnBadArgument, std::move(callback));
                return;
            }
            SetInputParams(current, [this, controlId, value, callback = std::move(callback)](
                                        IOReturn writeStatus) mutable {
                if (writeStatus != kIOReturnSuccess) {
                    FinishSemanticControlWrite(writeStatus, std::move(callback));
                    return;
                }
                GetInputParams([this, controlId, value, callback = std::move(callback)](
                                   IOReturn verifyStatus, InputParams readback) mutable {
                    if (verifyStatus == kIOReturnSuccess &&
                        InputControlMatches(readback, controlId, value)) {
                        PublishInputControlReadback(readback);
                        FinishSemanticControlWrite(kIOReturnSuccess, std::move(callback));
                    } else {
                        // Never advance driver belief on a lost software notice or a
                        // device-side rejected field. The next ordinary poll keeps the
                        // UI tied to the hardware, rather than to the requested value.
                        FinishSemanticControlWrite(
                            verifyStatus == kIOReturnSuccess ? kIOReturnError : verifyStatus,
                            std::move(callback));
                    }
                });
            });
        });
        return;
    }

    GetOutputGroupState([this, controlId, value, callback = std::move(callback)](
                            IOReturn readStatus, OutputGroupState current) mutable {
        if (readStatus != kIOReturnSuccess) {
            FinishSemanticControlWrite(readStatus, std::move(callback));
            return;
        }
        if (!ApplyOutputControl(current, controlId, value)) {
            FinishSemanticControlWrite(kIOReturnBadArgument, std::move(callback));
            return;
        }
        CommitOutputControlState(current, controlId,
                                 [this, controlId, value, callback = std::move(callback)](
                                     IOReturn writeStatus) mutable {
            if (writeStatus != kIOReturnSuccess) {
                FinishSemanticControlWrite(writeStatus, std::move(callback));
                return;
            }
            GetOutputGroupState([this, controlId, value, callback = std::move(callback)](
                                    IOReturn verifyStatus, OutputGroupState readback) mutable {
                if (verifyStatus == kIOReturnSuccess &&
                    OutputControlMatches(readback, controlId, value)) {
                    PublishOutputControlReadback(readback);
                    FinishSemanticControlWrite(kIOReturnSuccess, std::move(callback));
                } else {
                    FinishSemanticControlWrite(
                        verifyStatus == kIOReturnSuccess ? kIOReturnError : verifyStatus,
                        std::move(callback));
                }
            });
        });
    });
}

bool SPro24DspProtocol::BeginSemanticControlWrite(
    const Audio::IAudioControlSurface::ApplyCallback& callback) noexcept {
    if (!semanticControlLock_) {
        callback(kIOReturnNotReady);
        return false;
    }
    IOLockLock(semanticControlLock_);
    const IOReturn status = !semanticControls_.valid ? kIOReturnNotReady :
        semanticControls_.writeInFlight ? kIOReturnBusy : kIOReturnSuccess;
    if (status == kIOReturnSuccess) semanticControls_.writeInFlight = true;
    IOLockUnlock(semanticControlLock_);
    if (status != kIOReturnSuccess) callback(status);
    return status == kIOReturnSuccess;
}

void SPro24DspProtocol::FinishSemanticControlWrite(
    IOReturn status, Audio::IAudioControlSurface::ApplyCallback callback) noexcept {
    if (semanticControlLock_) {
        IOLockLock(semanticControlLock_);
        semanticControls_.writeInFlight = false;
        IOLockUnlock(semanticControlLock_);
    }
    if (callback) callback(status);
}

void SPro24DspProtocol::PublishInputControlReadback(const InputParams& input) noexcept {
    if (!semanticControlLock_) return;
    IOLockLock(semanticControlLock_);
    semanticControls_.input = input;
    ++semanticControls_.revision;
    IOLockUnlock(semanticControlLock_);
}

void SPro24DspProtocol::PublishOutputControlReadback(const OutputGroupState& output) noexcept {
    if (!semanticControlLock_) return;
    IOLockLock(semanticControlLock_);
    semanticControls_.output = output;
    ++semanticControls_.revision;
    IOLockUnlock(semanticControlLock_);
}

void SPro24DspProtocol::HandleExtensionSectionsRead(IOReturn status,
                                                    ExtensionSections sections,
                                                    InitCallback callback) {
    if (status != kIOReturnSuccess) {
        ASFW_LOG(DICE, "Failed to read Focusrite extension sections: 0x%x", status);
        callback(status);
        return;
    }

    extensionSections_ = sections;
    appSectionBase_ = ASFW::Audio::DICE::ExtensionAbsoluteOffset(extensionSections_.application);
    commandSectionBase_ = ASFW::Audio::DICE::ExtensionAbsoluteOffset(extensionSections_.command);
    routerSectionBase_ = ASFW::Audio::DICE::ExtensionAbsoluteOffset(extensionSections_.router);
    currentConfigBase_ = ASFW::Audio::DICE::ExtensionAbsoluteOffset(extensionSections_.currentConfig);

    if (appSectionBase_ == kDICEExtensionOffset ||
        commandSectionBase_ == kDICEExtensionOffset ||
        routerSectionBase_ == kDICEExtensionOffset ||
        currentConfigBase_ == kDICEExtensionOffset) {
        ASFW_LOG(DICE, "SPro24DspProtocol: missing required TCAT extension section(s)");
        callback(kIOReturnNotFound);
        return;
    }

    extensionsLoaded_ = true;
    ASFW_LOG(DICE,
             "SPro24DspProtocol: loaded Focusrite extension bases app=0x%08x cmd=0x%08x router=0x%08x current=0x%08x",
             appSectionBase_,
             commandSectionBase_,
             routerSectionBase_,
             currentConfigBase_);
    callback(kIOReturnSuccess);
}

void SPro24DspProtocol::EnsureExtensionsLoaded(VoidCallback callback) {
    if (extensionsLoaded_) {
        callback(kIOReturnSuccess);
        return;
    }

    tcat_.Transaction().ReadExtensionSections(
        [this, callback = std::move(callback)](IOReturn status, ExtensionSections sections) mutable {
            HandleExtensionSectionsRead(status, sections, std::move(callback));
        });
}

void SPro24DspProtocol::PrimeSemanticMatrix() noexcept {
    EnsureExtensionsLoaded([this](IOReturn sectionStatus) {
        if (sectionStatus != kIOReturnSuccess) {
            ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: extension sections 0x%x", sectionStatus);
            return;
        }
        tcat_.Transaction().ReadExtensionCaps(extensionSections_,
            [this](IOReturn capsStatus, DiceExtensionCaps caps) {
                if (capsStatus != kIOReturnSuccess) {
                    ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: extension caps 0x%x", capsStatus);
                    return;
                }
                // The editable router section is a staging image on this
                // device; the active image is in CURRENT_CONFIG. See
                // kSemanticRouterRateMode for which mode is read.
                tcat_.Transaction().ReadCurrentConfigRouterEntries(
                    extensionSections_, caps, kSemanticRouterRateMode,
                    [this, caps](IOReturn routeStatus, DiceRouterEntries routes) {
                        if (routeStatus != kIOReturnSuccess) {
                            ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: router 0x%x", routeStatus);
                            return;
                        }
                        tcat_.Transaction().ReadMixerCoefficients(extensionSections_, caps,
                            [this, caps, routes](IOReturn mixerStatus, DiceMixerCoefficients coefficients) {
                                if (mixerStatus != kIOReturnSuccess || !semanticMatrixLock_) {
                                    ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: mixer 0x%x", mixerStatus);
                                    return;
                                }
                                IOLockLock(semanticMatrixLock_);
                                semanticExtensionCaps_ = caps;
                                semanticRouterEntries_ = routes;
                                // The cached image and the mode it was read at
                                // must travel together: the signal table
                                // resolves a source differently per mode.
                                semanticRateMode_ = kSemanticRouterRateMode;
                                semanticMixerCoefficients_ = coefficients;
                                semanticMatrixReady_ = true;
                                ReconcileSemanticStripStates();
                                ++semanticMatrixRevision_;
                                IOLockUnlock(semanticMatrixLock_);
                                ASFW_LOG(DICE, "SPro24 semantic matrix cached: %ux%u routes=%u",
                                         coefficients.inputCount, coefficients.outputCount, routes.count);
                            });
                    });
            });
    });
}

void SPro24DspProtocol::PrimeSemanticControls() noexcept {
    EnsureExtensionsLoaded([this](IOReturn sectionStatus) {
        if (sectionStatus != kIOReturnSuccess) return;
        GetInputParams([this](IOReturn inputStatus, InputParams input) {
            if (inputStatus != kIOReturnSuccess) return;
            GetOutputGroupState([this, input](IOReturn outputStatus, OutputGroupState output) {
                if (outputStatus != kIOReturnSuccess) return;
                GetEffectParams([this, input, output](IOReturn effectStatus, EffectGeneralParams effects) {
                    if (effectStatus != kIOReturnSuccess) return;
                    GetCompressorState([this, input, output, effects](
                        IOReturn compressorStatus, CompressorState compressor) {
                        if (compressorStatus != kIOReturnSuccess) return;
                        GetReverbState([this, input, output, effects, compressor](
                            IOReturn reverbStatus, ReverbState reverb) {
                            if (reverbStatus != kIOReturnSuccess) return;
                            GetInSituMode([this, input, output, effects, compressor, reverb](
                                IOReturn inSituStatus, bool inSitu) {
                                if (inSituStatus != kIOReturnSuccess || !semanticControlLock_) return;
                                IOLockLock(semanticControlLock_);
                                semanticControls_.input = input;
                                semanticControls_.output = output;
                                semanticControls_.effects = effects;
                                semanticControls_.compressor = compressor;
                                semanticControls_.reverb = reverb;
                                semanticControls_.inSitu = inSitu;
                                semanticControls_.valid = true;
                                ++semanticControls_.revision;
                                IOLockUnlock(semanticControlLock_);
                                ASFW_LOG(DICE, "SPro24 semantic controls cached");
                            });
                        });
                    });
                });
            });
        });
    });
}

void SPro24DspProtocol::PrepareStoppedForRate(const AudioClockConfig& clock,
                                              VoidCallback callback) {
    // This hook is invoked by DICETcatProtocol only after the generic prepare
    // path has selected the requested clock and left GLOBAL_ENABLE clear.  The
    // extension command observed on hardware stops the DICE engine and emits
    // RX/TX configuration-change notices, so it must never be injected after
    // ProgramRx or while host DMA depends on the capture clock.
    // A reselected generic DICE clock often materializes the normal stream
    // records by itself.  Do not then inject the Focusrite load command: on
    // this hardware it invalidates otherwise writable RX/TX stream registers.
    // The command is cold-image recovery only, not normal-start decoration.
    Audio::AudioStreamRuntimeCaps genericCaps{};
    if (tcat_.GetRuntimeAudioStreamCaps(genericCaps) &&
        HasUsableGenericStreamImage(genericCaps)) {
        ASFW_LOG(DICE,
                 "[SPro24Bringup] generic stream image ready at %u Hz; skipping stopped extension load",
                 genericCaps.sampleRateHz);
        callback(kIOReturnSuccess);
        return;
    }

    const uint32_t rateHz = clock.sampleRateHz;
    LoadRouterStreamConfigForRate(
        rateHz,
        [this, rateHz, callback = std::move(callback)](IOReturn loadStatus) mutable {
            if (loadStatus != kIOReturnSuccess) {
                callback(loadStatus);
                return;
            }
            RearmFxForRate(rateHz, std::move(callback));
        });
}

void SPro24DspProtocol::LoadRouterStreamConfigForRate(uint32_t rateHz,
                                                       VoidCallback callback) {
    DiceRateMode mode{};
    if (!DiceRateModeForRate(rateHz, mode)) {
        callback(kIOReturnUnsupported);
        return;
    }

    CancelExtensionCommandPoll();
    const uint64_t epoch = extensionCommandEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    // The load command can report success while the device is still replacing
    // its stopped stream image. The device's RX/TX-config notification is the
    // completion edge used by the vendor Saffire kext; discard unrelated
    // earlier notifications before issuing this particular command.
    NotificationMailbox::Reset();
    const uint32_t opcode = ExtensionCommandOpcode::kExecute |
                            ExtensionRateFlag(mode) |
                            ExtensionCommandOpcode::kLoadRouterStreamConfig;

    EnsureExtensionsLoaded(
        [this, epoch, opcode, rateHz, callback = std::move(callback)](IOReturn sectionStatus) mutable {
            if (sectionStatus != kIOReturnSuccess) {
                callback(sectionStatus);
                return;
            }

            ASFW_LOG(DICE,
                     "[SPro24Bringup] stopped extension load rate=%u opcode=0x%08x command=0x%08x",
                     rateHz, opcode, commandSectionBase_);
            (void)tcat_.IO().WriteQuadBE(
                MakeDICEAddress(commandSectionBase_ + ExtensionCommandOffset::kOpcode),
                opcode,
                [this, epoch, callback = std::move(callback)](
                    Async::AsyncStatus transportStatus) mutable {
                    const IOReturn status =
                        Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus);
                    if (status != kIOReturnSuccess) {
                        callback(status);
                        return;
                    }
                    ScheduleExtensionCommandPoll(epoch, 0, std::move(callback));
                });
        });
}

void SPro24DspProtocol::ScheduleExtensionCommandPoll(uint64_t epoch,
                                                      uint32_t attempt,
                                                      VoidCallback callback) {
    if (extensionCommandEpoch_.load(std::memory_order_acquire) != epoch) {
        callback(kIOReturnAborted);
        return;
    }

    // Host tests use synchronous fake transactions and do not inject a timer.
    // Production always defers the first read by 50 ms, matching the TCAT
    // userspace reference instead of busy-waiting on a DriverKit queue.
    if (!timerScheduler_) {
        PollExtensionCommand(epoch, attempt, std::move(callback));
        return;
    }

    const auto token = timerScheduler_->ScheduleAfter(
        kExtensionCommandPollDelayNs,
        [this, epoch, attempt, callback]() mutable {
            extensionCommandTimer_.store(Scheduling::kInvalidTimerToken,
                                         std::memory_order_release);
            PollExtensionCommand(epoch, attempt, std::move(callback));
        });
    if (token == Scheduling::kInvalidTimerToken) {
        callback(kIOReturnNotReady);
        return;
    }
    extensionCommandTimer_.store(token, std::memory_order_release);
}

void SPro24DspProtocol::PollExtensionCommand(uint64_t epoch,
                                             uint32_t attempt,
                                             VoidCallback callback) {
    if (extensionCommandEpoch_.load(std::memory_order_acquire) != epoch) {
        callback(kIOReturnAborted);
        return;
    }

    (void)tcat_.IO().ReadQuadBE(
        MakeDICEAddress(commandSectionBase_ + ExtensionCommandOffset::kOpcode),
        [this, epoch, attempt, callback = std::move(callback)](
            Async::AsyncStatus transportStatus, uint32_t opcode) mutable {
            const IOReturn status =
                Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus);
            if (status != kIOReturnSuccess) {
                callback(status);
                return;
            }
            if ((opcode & ExtensionCommandOpcode::kExecute) != 0) {
                if (attempt + 1 >= kExtensionCommandPollLimit) {
                    ASFW_LOG_ERROR(DICE,
                                   "[SPro24Bringup] extension command timed out opcode=0x%08x",
                                   opcode);
                    callback(kIOReturnTimeout);
                    return;
                }
                if (!timerScheduler_) {
                    callback(kIOReturnNotReady);
                    return;
                }
                ScheduleExtensionCommandPoll(epoch, attempt + 1,
                                             std::move(callback));
                return;
            }

            (void)tcat_.IO().ReadQuadBE(
                MakeDICEAddress(commandSectionBase_ + ExtensionCommandOffset::kReturn),
                [this, epoch, opcode, callback = std::move(callback)](
                    Async::AsyncStatus returnTransportStatus, uint32_t returnCode) mutable {
                    const IOReturn returnStatus =
                        Protocols::Ports::MapAsyncStatusToIOReturn(returnTransportStatus);
                    if (returnStatus != kIOReturnSuccess) {
                        callback(returnStatus);
                        return;
                    }
                    ASFW_LOG(DICE,
                             "[SPro24Bringup] extension load complete opcode=0x%08x return=0x%08x",
                             opcode, returnCode);
                    if (returnCode != 0) {
                        callback(kIOReturnError);
                        return;
                    }
                    WaitForRouterStreamConfigNotice(
                        epoch, 0, NotificationMailbox::Consume(), std::move(callback));
                });
        });
}

void SPro24DspProtocol::WaitForRouterStreamConfigNotice(uint64_t epoch,
                                                         uint32_t attempt,
                                                         uint32_t observedBits,
                                                         VoidCallback callback) {
    if (extensionCommandEpoch_.load(std::memory_order_acquire) != epoch) {
        callback(kIOReturnAborted);
        return;
    }

    const uint32_t bits = observedBits | NotificationMailbox::Consume();
    if ((bits & kRouterStreamConfigNoticeMask) == kRouterStreamConfigNoticeMask) {
        ASFW_LOG(DICE,
                 "[SPro24Bringup] stopped stream image acknowledged bits=0x%08x",
                 bits);
        callback(kIOReturnSuccess);
        return;
    }

    if (attempt >= kRouterStreamConfigNoticePollLimit) {
        ASFW_LOG_ERROR(DICE,
                       "[SPro24Bringup] timed out waiting for RX/TX config change bits=0x%08x",
                       bits);
        callback(kIOReturnTimeout);
        return;
    }

    // The host-test bus is synchronous. It must model the product's notify
    // transaction explicitly; silently accepting an unacknowledged command
    // would recreate the cold-start race this barrier prevents.
    if (!timerScheduler_) {
        ASFW_LOG_ERROR(DICE,
                       "[SPro24Bringup] missing RX/TX config-change acknowledgement bits=0x%08x",
                       bits);
        callback(kIOReturnNotReady);
        return;
    }

    const auto token = timerScheduler_->ScheduleAfter(
        kRouterStreamConfigNoticePollDelayNs,
        [this, epoch, attempt, bits, callback]() mutable {
            extensionCommandTimer_.store(Scheduling::kInvalidTimerToken,
                                         std::memory_order_release);
            WaitForRouterStreamConfigNotice(epoch, attempt + 1, bits, std::move(callback));
        });
    if (token == Scheduling::kInvalidTimerToken) {
        callback(kIOReturnNotReady);
        return;
    }
    extensionCommandTimer_.store(token, std::memory_order_release);
}

void SPro24DspProtocol::CancelExtensionCommandPoll() noexcept {
    extensionCommandEpoch_.fetch_add(1, std::memory_order_acq_rel);
    const uint64_t timer = extensionCommandTimer_.exchange(
        Scheduling::kInvalidTimerToken, std::memory_order_acq_rel);
    if (timer != Scheduling::kInvalidTimerToken && timerScheduler_) {
        timerScheduler_->Cancel(timer);
    }
}

void SPro24DspProtocol::RearmFxForRate(uint32_t rateHz, VoidCallback callback) {
    uint32_t bankOffset = 0;
    if (!FxBankOffsetForRate(rateHz, bankOffset)) {
        ASFW_LOG(DICE, "SPro24 FX re-arm skipped: unsupported active rate %u Hz", rateHz);
        callback(kIOReturnUnsupported);
        return;
    }

    auto snapshot = std::make_shared<FxRearmSnapshot>();
    snapshot->coefficientBankOffset = bankOffset;

    EnsureExtensionsLoaded([this, snapshot, callback = std::move(callback)](IOReturn sectionStatus) mutable {
        if (sectionStatus != kIOReturnSuccess) {
            callback(sectionStatus);
            return;
        }

        // 0 = normal FX path; 1 = InSitu/VRM. The vendor has a separate
        // coefficient writer for InSitu, so never send the FX sequence into
        // that mode.
        ReadAppQuad(kDspEnableOffset,
            [this, snapshot, callback = std::move(callback)](IOReturn modeStatus, uint32_t mode) mutable {
                if (modeStatus != kIOReturnSuccess) {
                    callback(modeStatus);
                    return;
                }
                if (mode != 0) {
                    ASFW_LOG(DICE, "SPro24 FX re-arm skipped: InSitu/VRM mode=%u", mode);
                    callback(kIOReturnSuccess);
                    return;
                }
                ReadFxRearmSnapshot(snapshot,
                    [this, snapshot, callback = std::move(callback)](IOReturn readStatus) mutable {
                        if (readStatus != kIOReturnSuccess) {
                            callback(readStatus);
                            return;
                        }
                        ReplayFxSnapshot(snapshot, std::move(callback));
                    });
            });
    });
}

void SPro24DspProtocol::ReadFxRearmSnapshot(const std::shared_ptr<FxRearmSnapshot>& snapshot,
                                            VoidCallback callback) {
    ReadAppSection(kInputOffset, snapshot->input.size(),
        [this, snapshot, callback = std::move(callback)](
            IOReturn inputStatus, const uint8_t* input, size_t inputSize) mutable {
            if (inputStatus != kIOReturnSuccess || inputSize != snapshot->input.size()) {
                callback(inputStatus == kIOReturnSuccess ? kIOReturnUnderrun : inputStatus);
                return;
            }
            __builtin_memcpy(snapshot->input.data(), input, snapshot->input.size());
            ReadAppQuad(kEffectGeneralOffset,
                [this, snapshot, callback = std::move(callback)](IOReturn flagsStatus, uint32_t flags) mutable {
                    if (flagsStatus != kIOReturnSuccess) {
                        callback(flagsStatus);
                        return;
                    }
                    snapshot->channelStripFlags = flags;
                    ReadAppSection(snapshot->coefficientBankOffset, snapshot->coefficientBank->size(),
                        [snapshot, callback = std::move(callback)](
                            IOReturn coefficientStatus, const uint8_t* coefficients, size_t coefficientSize) mutable {
                            if (coefficientStatus != kIOReturnSuccess ||
                                coefficientSize != snapshot->coefficientBank->size()) {
                                callback(coefficientStatus == kIOReturnSuccess
                                             ? kIOReturnUnderrun : coefficientStatus);
                                return;
                            }
                            __builtin_memcpy(snapshot->coefficientBank->data(), coefficients,
                                             snapshot->coefficientBank->size());
                            callback(kIOReturnSuccess);
                        });
                });
        });
}

void SPro24DspProtocol::ReplayFxSnapshot(const std::shared_ptr<FxRearmSnapshot>& snapshot,
                                         VoidCallback callback) {
    // CPro24DSPUserDevice::SetState order: input, EQ + flags, compressor +
    // flags, then reverb. The payload is a live snapshot, never a preset.
    WriteAppSection(kInputOffset, snapshot->input.data(), snapshot->input.size(),
        [this, snapshot, callback = std::move(callback)](IOReturn inputWriteStatus) mutable {
            if (inputWriteStatus != kIOReturnSuccess) {
                callback(inputWriteStatus);
                return;
            }
            SendSwNotice(SwNotice::InputParams,
                [this, snapshot, callback = std::move(callback)](IOReturn inputNoticeStatus) mutable {
                    if (inputNoticeStatus != kIOReturnSuccess) {
                        callback(inputNoticeStatus);
                        return;
                    }
                    ReplayFxGroup(snapshot, FxWriteGroup::Equalizer,
                        kEqNotices.data(), kEqNotices.size(),
                        [this, snapshot, callback = std::move(callback)](IOReturn eqStatus) mutable {
                            if (eqStatus != kIOReturnSuccess) {
                                callback(eqStatus);
                                return;
                            }
                            ReplayChannelStripFlags(snapshot,
                                [this, snapshot, callback = std::move(callback)](IOReturn eqFlagsStatus) mutable {
                                    if (eqFlagsStatus != kIOReturnSuccess) {
                                        callback(eqFlagsStatus);
                                        return;
                                    }
                                    ReplayFxGroup(snapshot, FxWriteGroup::Compressor,
                                        kCompressorNotice.data(), kCompressorNotice.size(),
                                        [this, snapshot, callback = std::move(callback)](IOReturn compressorStatus) mutable {
                                            if (compressorStatus != kIOReturnSuccess) {
                                                callback(compressorStatus);
                                                return;
                                            }
                                            ReplayChannelStripFlags(snapshot,
                                                [this, snapshot, callback = std::move(callback)](IOReturn compressorFlagsStatus) mutable {
                                                    if (compressorFlagsStatus != kIOReturnSuccess) {
                                                        callback(compressorFlagsStatus);
                                                        return;
                                                    }
                                                    ReplayFxGroup(snapshot, FxWriteGroup::Reverb,
                                                        kReverbNotice.data(), kReverbNotice.size(),
                                                        std::move(callback));
                                                });
                                        });
                                });
                        });
                });
        });
}

void SPro24DspProtocol::ReplayFxGroup(const std::shared_ptr<FxRearmSnapshot>& snapshot,
                                      FxWriteGroup group,
                                      const SwNotice* notices,
                                      size_t noticeCount,
                                      VoidCallback callback) {
    WriteFxFragments(snapshot->coefficientBank, snapshot->coefficientBankOffset, group, 0,
        [this, notices, noticeCount, callback = std::move(callback)](IOReturn writeStatus) mutable {
            if (writeStatus != kIOReturnSuccess) {
                callback(writeStatus);
                return;
            }
            SendSwNotices(notices, noticeCount, 0, std::move(callback));
        });
}

void SPro24DspProtocol::ReplayChannelStripFlags(
    const std::shared_ptr<FxRearmSnapshot>& snapshot,
    VoidCallback callback) {
    WriteAppQuad(kEffectGeneralOffset, snapshot->channelStripFlags,
        [this, callback = std::move(callback)](IOReturn writeStatus) mutable {
            if (writeStatus != kIOReturnSuccess) {
                callback(writeStatus);
                return;
            }
            SendSwNotices(kChannelStripNotice.data(), kChannelStripNotice.size(), 0,
                          std::move(callback));
        });
}

void SPro24DspProtocol::WriteFxFragments(
    const std::shared_ptr<std::array<uint8_t, kFxCoefficientBankSize>>& bank,
    uint32_t bankOffset,
    FxWriteGroup group,
    size_t index,
    VoidCallback callback) {
    const FxWriteFragment* fragments = nullptr;
    size_t count = 0;
    switch (group) {
    case FxWriteGroup::Equalizer:
        fragments = kEqFragments.data();
        count = kEqFragments.size();
        break;
    case FxWriteGroup::Compressor:
        fragments = kCompressorFragments.data();
        count = kCompressorFragments.size();
        break;
    case FxWriteGroup::Reverb:
        fragments = kReverbFragments.data();
        count = kReverbFragments.size();
        break;
    }

    if (index == count) {
        callback(kIOReturnSuccess);
        return;
    }

    const auto fragment = fragments[index];
    const size_t byteOffset = fragment.offset;
    const size_t byteCount = size_t{fragment.quadlets} * sizeof(uint32_t);
    WriteAppSection(bankOffset + fragment.offset, bank->data() + byteOffset, byteCount,
        [this, bank, bankOffset, group, index, callback = std::move(callback)](IOReturn status) mutable {
            if (status != kIOReturnSuccess) {
                callback(status);
                return;
            }
            WriteFxFragments(bank, bankOffset, group, index + 1, std::move(callback));
        });
}

void SPro24DspProtocol::SendSwNotices(const SwNotice* notices,
                                      size_t count,
                                      size_t index,
                                      VoidCallback callback) {
    if (index == count) {
        callback(kIOReturnSuccess);
        return;
    }
    SendSwNotice(notices[index],
        [this, notices, count, index, callback = std::move(callback)](IOReturn status) mutable {
            if (status != kIOReturnSuccess) {
                callback(status);
                return;
            }
            SendSwNotices(notices, count, index + 1, std::move(callback));
        });
}

IOReturn SPro24DspProtocol::Shutdown() {
    ASFW_LOG(DICE, "SPro24DspProtocol::Shutdown");
    CancelExtensionCommandPoll();
    extensionSections_ = {};
    appSectionBase_ = 0;
    commandSectionBase_ = 0;
    routerSectionBase_ = 0;
    currentConfigBase_ = 0;
    extensionsLoaded_ = false;
    if (semanticMatrixLock_) {
        IOLockLock(semanticMatrixLock_);
        semanticMixerCoefficients_ = {};
        semanticRouterEntries_ = {};
        semanticMatrixReady_ = false;
        ++semanticMatrixRevision_;
        IOLockUnlock(semanticMatrixLock_);
    }
    return tcat_.Shutdown();
}

void SPro24DspProtocol::PrepareDuplex48k(const AudioDuplexChannels& channels, VoidCallback callback) {
    // DICETcatProtocol's stopped-prepare hook owns the SPro-specific extension
    // load and FX replay for both this compatibility entry point and the
    // protocol-neutral coordinator path.
    tcat_.PrepareDuplex48k(channels, std::move(callback));
}

void SPro24DspProtocol::ProgramRxForDuplex48k(VoidCallback callback) {
    tcat_.ProgramRxForDuplex48k(std::move(callback));
}

void SPro24DspProtocol::ProgramTxAndEnableDuplex48k(VoidCallback callback) {
    tcat_.ProgramTxAndEnableDuplex48k(std::move(callback));
}

void SPro24DspProtocol::ConfirmDuplex48kStart(VoidCallback callback) {
    tcat_.ConfirmDuplex48kStart(std::move(callback));
}

IOReturn SPro24DspProtocol::StopDuplex() {
    return tcat_.StopDuplex();
}

void SPro24DspProtocol::UpdateRuntimeContext(const Discovery::DeviceRouteToken& route,
                                             Protocols::AVC::FCPTransport* transport) {
    tcat_.UpdateRuntimeContext(route, transport);
}

void SPro24DspProtocol::ReadAppQuad(uint32_t offset,
                                    std::function<void(IOReturn, uint32_t)> callback) {
    EnsureExtensionsLoaded([this, offset, callback = std::move(callback)](IOReturn status) mutable {
        if (status != kIOReturnSuccess) {
            callback(status, 0);
            return;
        }

        (void)tcat_.IO().ReadQuadBE(MakeDICEAddress(appSectionBase_ + offset),
                              [callback = std::move(callback)](Async::AsyncStatus transportStatus, uint32_t value) mutable {
                                  callback(Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus), value);
                              });
    });
}

void SPro24DspProtocol::WriteAppQuad(uint32_t offset, uint32_t value, VoidCallback callback) {
    EnsureExtensionsLoaded([this, offset, value, callback = std::move(callback)](IOReturn status) mutable {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }

        (void)tcat_.IO().WriteQuadBE(MakeDICEAddress(appSectionBase_ + offset),
                               value,
                               [callback = std::move(callback)](Async::AsyncStatus transportStatus) mutable {
                                   callback(Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus));
                               });
    });
}

void SPro24DspProtocol::ReadAppSection(uint32_t offset, size_t size, DICEReadCallback callback) {
    auto callbackState = Common::ShareCallback(std::move(callback));
    EnsureExtensionsLoaded([this, offset, size, callbackState](IOReturn status) mutable {
        if (status != kIOReturnSuccess) {
            Common::InvokeSharedCallback(callbackState, status, nullptr, size_t{0});
            return;
        }

        (void)tcat_.IO().ReadBlock(MakeDICEAddress(appSectionBase_ + offset),
                             static_cast<uint32_t>(size),
                             [callbackState](Async::AsyncStatus transportStatus, std::span<const uint8_t> payload) {
                                 const bool hasPayload = transportStatus == Async::AsyncStatus::kSuccess ||
                                                         transportStatus == Async::AsyncStatus::kShortRead;
                                 Common::InvokeSharedCallback(callbackState,
                                                             Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus),
                                                             hasPayload ? payload.data() : nullptr,
                                                             hasPayload ? payload.size() : size_t{0});
                             });
    });
}

void SPro24DspProtocol::WriteAppSection(uint32_t offset,
                                        const uint8_t* data,
                                        size_t size,
                                        DICEWriteCallback callback) {
    auto callbackState = Common::ShareCallback(std::move(callback));
    EnsureExtensionsLoaded([this, offset, data, size, callbackState](IOReturn status) mutable {
        if (status != kIOReturnSuccess) {
            Common::InvokeSharedCallback(callbackState, status);
            return;
        }

        (void)tcat_.IO().WriteBlock(MakeDICEAddress(appSectionBase_ + offset),
                              std::span<const uint8_t>(data, size),
                              [callbackState](Async::AsyncStatus transportStatus) {
                                  Common::InvokeSharedCallback(callbackState,
                                                              Protocols::Ports::MapAsyncStatusToIOReturn(transportStatus));
                              });
    });
}

void SPro24DspProtocol::SendSwNotice(SwNotice notice, VoidCallback callback) {
    WriteAppQuad(kSwNoticeOffset, static_cast<uint32_t>(notice), std::move(callback));
}

void SPro24DspProtocol::SetInSituMode(bool enable, VoidCallback callback) {
    uint32_t value = enable ? 1 : 0;
    WriteAppQuad(kDspEnableOffset, value, [this, callback](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        SendSwNotice(SwNotice::InSituMode, callback);
    });
}

void SPro24DspProtocol::GetInSituMode(ResultCallback<bool> callback) {
    ReadAppQuad(kDspEnableOffset, [callback = std::move(callback)](IOReturn status, uint32_t value) mutable {
        callback(status, status == kIOReturnSuccess && value != 0);
    });
}

void SPro24DspProtocol::GetEffectParams(ResultCallback<EffectGeneralParams> callback) {
    ReadAppQuad(kEffectGeneralOffset, [callback](IOReturn status, uint32_t value) {
        if (status != kIOReturnSuccess) {
            callback(status, {});
            return;
        }
        uint8_t data[4];
        ASFW::FW::WriteBE32(data, value);
        callback(kIOReturnSuccess, EffectGeneralParams::Deserialize(data));
    });
}

void SPro24DspProtocol::SetEffectParams(const EffectGeneralParams& params, VoidCallback callback) {
    uint8_t data[4];
    params.Serialize(data);
    const uint32_t value = ASFW::FW::ReadBE32(data);
    
    WriteAppQuad(kEffectGeneralOffset, value, [this, callback](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        SendSwNotice(SwNotice::EffectChanged, callback);
    });
}

void SPro24DspProtocol::GetCompressorState(ResultCallback<CompressorState> callback) {
    ReadAppSection(kCoefOffset + CoefBlock::kCompressor * kCoefBlockSize, 2 * kCoefBlockSize,
                   [callback](IOReturn status, const uint8_t* data, size_t /*size*/) {
        if (status != kIOReturnSuccess) {
            callback(status, {});
            return;
        }
        callback(kIOReturnSuccess, CompressorState::Deserialize(data));
    });
}

void SPro24DspProtocol::SetCompressorState(const CompressorState& state, VoidCallback callback) {
    // Note: Need to allocate buffer that outlives async call
    auto buffer = std::make_shared<std::array<uint8_t, 2 * kCoefBlockSize>>();
    state.Serialize(buffer->data());

    WriteAppSection(kCoefOffset + CoefBlock::kCompressor * kCoefBlockSize, buffer->data(), buffer->size(),
                    [this, callback, buffer](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        // Per Linux reference (spro24dsp.rs lines 770-771): send BOTH
        // CompCh0 and CompCh1 SW notices after compressor state write.
        SendSwNotice(SwNotice::CompCh0, [this, callback](IOReturn s1) {
            if (s1 != kIOReturnSuccess) {
                callback(s1);
                return;
            }
            SendSwNotice(SwNotice::CompCh1, callback);
        });
    });
}

void SPro24DspProtocol::GetReverbState(ResultCallback<ReverbState> callback) {
    ReadAppSection(kCoefOffset + CoefBlock::kReverb * kCoefBlockSize, kCoefBlockSize,
                   [callback](IOReturn status, const uint8_t* data, size_t /*size*/) {
        if (status != kIOReturnSuccess) {
            callback(status, {});
            return;
        }
        callback(kIOReturnSuccess, ReverbState::Deserialize(data));
    });
}

void SPro24DspProtocol::SetReverbState(const ReverbState& state, VoidCallback callback) {
    auto buffer = std::make_shared<std::array<uint8_t, kCoefBlockSize>>();
    state.Serialize(buffer->data());

    WriteAppSection(kCoefOffset + CoefBlock::kReverb * kCoefBlockSize, buffer->data(), buffer->size(),
                    [this, callback, buffer](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        // Per Linux reference: REVERB_SW_NOTICE = 0x1A
        SendSwNotice(SwNotice::Reverb, callback);
    });
}

void SPro24DspProtocol::GetInputParams(ResultCallback<InputParams> callback) {
    ReadAppSection(kInputOffset, 8,
                   [callback](IOReturn status, const uint8_t* data, size_t /*size*/) {
        if (status != kIOReturnSuccess) {
            callback(status, {});
            return;
        }
        callback(kIOReturnSuccess, InputParams::Deserialize(data));
    });
}

void SPro24DspProtocol::SetInputParams(const InputParams& params, VoidCallback callback) {
    auto buffer = std::make_shared<std::array<uint8_t, 8>>();
    params.Serialize(buffer->data());

    WriteAppSection(kInputOffset, buffer->data(), buffer->size(),
                    [this, callback, buffer](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        SendSwNotice(SwNotice::InputChanged, callback);
    });
}

void SPro24DspProtocol::GetOutputGroupState(ResultCallback<OutputGroupState> callback) {
    ReadAppSection(kOutputGroupOffset, kOutputGroupStateSize,
                   [callback](IOReturn status, const uint8_t* data, size_t size) {
        if (status != kIOReturnSuccess) {
            callback(status, {});
            return;
        }
        if (size < kOutputGroupStateSize) {
            callback(kIOReturnUnderrun, {});
            return;
        }
        callback(kIOReturnSuccess, OutputGroupState::Deserialize(data));
    });
}

void SPro24DspProtocol::SetOutputGroupState(const OutputGroupState& state, VoidCallback callback) {
    auto buffer = std::make_shared<std::array<uint8_t, kOutputGroupStateSize>>();
    state.Serialize(buffer->data());

    WriteAppSection(kOutputGroupOffset, buffer->data(), buffer->size(),
                    [this, callback, buffer](IOReturn status) {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        SendSwNotice(SwNotice::DimMute, [this, callback](IOReturn dimStatus) {
            if (dimStatus != kIOReturnSuccess) {
                callback(dimStatus);
                return;
            }
            SendSwNotice(SwNotice::OutputSrc, callback);
        });
    });
}

void SPro24DspProtocol::CommitOutputControlState(const OutputGroupState& state,
                                                 uint32_t controlId,
                                                 VoidCallback callback) {
    auto buffer = std::make_shared<std::array<uint8_t, kOutputGroupStateSize>>();
    state.Serialize(buffer->data());

    WriteAppSection(kOutputGroupOffset, buffer->data(), buffer->size(),
                    [this, controlId, callback = std::move(callback), buffer](IOReturn status) mutable {
        if (status != kIOReturnSuccess) {
            callback(status);
            return;
        }
        // Cross-validated with snd-firewire-ctl-services focusrite.rs:298-303:
        // mute/dim occupies the first two quadlets; lane volume/mute occupies
        // the subsequent group state.  A one-field semantic write sends only
        // the notice for the touched domain, avoiding an unrelated output
        // source refresh.
        const SwNotice notice = controlId == SPro24DspControl::kGlobalMute ||
                                     controlId == SPro24DspControl::kGlobalDim
            ? SwNotice::DimMute
            : SwNotice::OutputSrc;
        SendSwNotice(notice, std::move(callback));
    });
}

// ============================================================================
// TODO: Test only - Stream Control
// ============================================================================

void SPro24DspProtocol::StartStreamTest(VoidCallback callback) {
    const AudioDuplexChannels channels{};
    PrepareDuplex48k(channels, std::move(callback));
}

} // namespace ASFW::Audio::DICE::Focusrite
