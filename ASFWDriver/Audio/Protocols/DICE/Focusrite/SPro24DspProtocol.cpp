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
#include <array>
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
    if (status == kIOReturnSuccess) PrimeSemanticMatrix();
}

bool SPro24DspProtocol::CopyAudioSemanticMatrix(
    Audio::AudioSemanticMatrixSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    if (!semanticMatrixLock_) return false;
    IOLockLock(semanticMatrixLock_);
    const bool ready = semanticMatrixReady_ &&
        BuildSPro24DspSemanticMatrix(semanticMixerCoefficients_, semanticRouterEntries_, outSnapshot);
    if (ready) outSnapshot.stateRevision = semanticMatrixRevision_;
    IOLockUnlock(semanticMatrixLock_);
    return ready;
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
                // device. The active low-rate image is in CURRENT_CONFIG.
                // SPro24 currently publishes only the 1x modes (44.1/48 kHz),
                // both of which use that low router block.
                tcat_.Transaction().ReadCurrentConfigRouterEntries(
                    extensionSections_, caps, DiceRateMode::Low,
                    [this, caps](IOReturn routeStatus, DiceRouterEntries routes) {
                        if (routeStatus != kIOReturnSuccess) {
                            ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: router 0x%x", routeStatus);
                            return;
                        }
                        tcat_.Transaction().ReadMixerCoefficients(extensionSections_, caps,
                            [this, routes](IOReturn mixerStatus, DiceMixerCoefficients coefficients) {
                                if (mixerStatus != kIOReturnSuccess || !semanticMatrixLock_) {
                                    ASFW_LOG(DICE, "SPro24 semantic matrix unavailable: mixer 0x%x", mixerStatus);
                                    return;
                                }
                                IOLockLock(semanticMatrixLock_);
                                semanticRouterEntries_ = routes;
                                semanticMixerCoefficients_ = coefficients;
                                semanticMatrixReady_ = true;
                                ++semanticMatrixRevision_;
                                IOLockUnlock(semanticMatrixLock_);
                                ASFW_LOG(DICE, "SPro24 semantic matrix cached: %ux%u routes=%u",
                                         coefficients.inputCount, coefficients.outputCount, routes.count);
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

// ============================================================================
// TODO: Test only - Stream Control
// ============================================================================

void SPro24DspProtocol::StartStreamTest(VoidCallback callback) {
    const AudioDuplexChannels channels{};
    PrepareDuplex48k(channels, std::move(callback));
}

} // namespace ASFW::Audio::DICE::Focusrite
