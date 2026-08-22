//
// ASFWAudioDriverIO.cpp
// ASFWDriver
//
// Real-time IO callback installation for ASFWAudioDriver.
//

#include "ASFWAudioDriverPrivate.hpp"
#include "../Runtime/PlaybackRingRange.hpp"
#include "../../Logging/Logging.hpp"

#include <DriverKit/DriverKit.h>

#include <atomic>
#include <cstring>

namespace ASFW::Audio::DriverKit {
namespace {

void PublishPlaybackRingWriteEnd(ASFW::Audio::Runtime::AudioGraphBinding& graph,
                                 ASFW::Audio::Runtime::AudioTransportControlBlock& control) noexcept {
    const uint64_t writeStart =
        control.client.outputWriteEndSampleFrame.load(std::memory_order_relaxed);
    const uint64_t writeEnd = control.client.OutputWrittenEndFrame();
    const uint64_t previous =
        control.playbackRingWriteFrame.load(std::memory_order_acquire);
    const uint64_t previousOldest =
        control.playbackRingOldestValidFrame.load(std::memory_order_acquire);
    const uint64_t consumed =
        control.playbackRingReadFrame.load(std::memory_order_acquire);
    const uint32_t capacity = graph.memory.outputFrameCapacity;
    const auto update = ASFW::Audio::Runtime::UpdatePlaybackRingRange(
        previous, previousOldest, writeStart, writeEnd, consumed, capacity);
    if (update.writtenEndFrame == previous) {
        return;
    }

    control.playbackRingOldestValidFrame.store(update.oldestValidFrame,
                                               std::memory_order_relaxed);
    if (update.discontinuity) {
        control.playbackRingDiscontinuityGeneration.fetch_add(1, std::memory_order_relaxed);
        control.discontinuities.fetch_add(1, std::memory_order_relaxed);
    }
    if (update.overrun) {
        control.playbackRingOverruns.fetch_add(1, std::memory_order_relaxed);
    }
    control.playbackRingWriteFrame.store(update.writtenEndFrame, std::memory_order_release);
}


void ZeroInputFrameIfMissing(ASFW::Audio::Runtime::AudioGraphBinding& graph,
                             uint64_t absoluteFrame) noexcept {
    auto* frame = graph.memory.InputFrame(absoluteFrame);
    if (!frame || graph.memory.inputChannels == 0) {
        return;
    }
    std::memset(frame,
                0,
                static_cast<size_t>(graph.memory.inputChannels) * sizeof(int32_t));
}

// A fixed budget spent from the first BeginRead is the wrong instrument here.
// On a device whose stream warms up with NO-DATA the entire budget burns during
// the warm-up window, every record reads `write=0`, and the steady state — the
// only state that says whether capture works — is never sampled. Report the
// *verdict* instead and log only when it changes, so a run costs a couple of
// lines and any transition into or out of starvation is always captured.
enum class CaptureReadVerdict : uint8_t {
    kUnknown = 0,
    kHealthy,        ///< Every requested frame came from the writer.
    kPartialStarve,  ///< Some frames were zero-filled.
    kTotalStarve,    ///< Nothing overlapped the writer's range.
};

[[nodiscard]] const char* CaptureReadVerdictName(CaptureReadVerdict verdict) noexcept {
    switch (verdict) {
        case CaptureReadVerdict::kHealthy:       return "healthy";
        case CaptureReadVerdict::kPartialStarve: return "partial-starve";
        case CaptureReadVerdict::kTotalStarve:   return "total-starve";
        case CaptureReadVerdict::kUnknown:       break;
    }
    return "unknown";
}

std::atomic<uint32_t> gCaptureReadVerdict{
    static_cast<uint32_t>(CaptureReadVerdict::kUnknown)};

// Transitions are rare by construction, but a stream oscillating on the edge of
// the ring could still flap at the IO rate. Cap the total so it can never
// become a hot-path log source.
constexpr uint32_t kCaptureReadTransitionBudget = 24;
std::atomic<uint32_t> gCaptureReadTransitionBudget{kCaptureReadTransitionBudget};

// Entry-level evidence. An absent [RxRead] record has four indistinguishable
// causes: the HAL never calls us, `running` is false, `skeletonBound` is false,
// or BeginRead is rejected by the ring-capacity guard before any work happens.
// Logging at the top of the handler, before every gate, separates them.
constexpr uint32_t kIoCallbackLogBudget = 24;
std::atomic<uint32_t> gIoCallbackLogBudget{kIoCallbackLogBudget};

bool PrepareCaptureRingForBeginRead(ASFW::Audio::Runtime::AudioGraphBinding& graph,
                                    ASFW::Audio::Runtime::AudioTransportControlBlock& control,
                                    uint64_t sampleTime,
                                    uint32_t frameCount) noexcept {
    if (frameCount == 0) {
        return true;
    }
    if (!graph.HasInput()) {
        return false;
    }

    const uint64_t write =
        control.captureRingWriteFrame.load(std::memory_order_acquire);
    const uint32_t capacity = graph.memory.inputFrameCapacity;
    const uint64_t oldest = (capacity != 0 && write > capacity) ? (write - capacity) : 0;
    bool starved = false;
    uint32_t starvedFrames = 0;
    for (uint32_t i = 0; i < frameCount; ++i) {
        const uint64_t frame = sampleTime + i;
        if (frame < oldest || frame >= write) {
            ZeroInputFrameIfMissing(graph, frame);
            starved = true;
            ++starvedFrames;
        }
    }

    // The HAL reads at `sampleTime`, which comes from the ZTS timeline; the RX
    // consumer writes at its own absolute frame cursor. If those two numbering
    // schemes do not share an origin, almost every requested frame falls
    // outside [oldest, write) and is zero-filled, and capture presents as
    // silence broken by isolated samples wherever the ranges happen to overlap.
    // `delta` is the whole diagnosis: 0 means the writer is exactly at the read
    // point, a large or drifting value means the two timelines are unrelated.
    const CaptureReadVerdict verdict =
        starvedFrames == 0          ? CaptureReadVerdict::kHealthy
        : starvedFrames < frameCount ? CaptureReadVerdict::kPartialStarve
                                     : CaptureReadVerdict::kTotalStarve;
    const auto previousVerdict = static_cast<CaptureReadVerdict>(
        gCaptureReadVerdict.exchange(static_cast<uint32_t>(verdict),
                                     std::memory_order_relaxed));
    if (verdict != previousVerdict &&
        gCaptureReadTransitionBudget.load(std::memory_order_relaxed) != 0) {
        gCaptureReadTransitionBudget.fetch_sub(1, std::memory_order_relaxed);
        ASFW_LOG(DirectAudio,
                 "[RxRead] %{public}s -> %{public}s sampleTime=%llu frames=%u "
                 "write=%llu oldest=%llu delta=%lld starvedFrames=%u capacity=%u",
                 CaptureReadVerdictName(previousVerdict),
                 CaptureReadVerdictName(verdict),
                 sampleTime, frameCount, write, oldest,
                 static_cast<long long>(static_cast<int64_t>(write) -
                                        static_cast<int64_t>(sampleTime)),
                 starvedFrames, capacity);
    }

    const uint64_t readEnd = sampleTime + frameCount;
    const uint64_t previousRead =
        control.captureRingReadFrame.load(std::memory_order_acquire);
    if (readEnd > previousRead) {
        control.captureRingReadFrame.store(readEnd, std::memory_order_release);
    }
    if (starved) {
        control.captureRingStarvations.fetch_add(1, std::memory_order_relaxed);
        control.rxCaptureBufferTelemetry.RecordStarvation(starvedFrames);
    }
    control.rxCaptureBufferTelemetry.Observe(
        write, readEnd, capacity);
    return true;
}

} // namespace

kern_return_t InstallIOOperationHandler(IOUserAudioDevice& audioDevice,
                                        ASFWAudioDriver_IVars& ivars) noexcept {
    gCaptureReadVerdict.store(static_cast<uint32_t>(CaptureReadVerdict::kUnknown),
                              std::memory_order_relaxed);
    gCaptureReadTransitionBudget.store(kCaptureReadTransitionBudget,
                                       std::memory_order_relaxed);
    gIoCallbackLogBudget.store(kIoCallbackLogBudget, std::memory_order_relaxed);
    auto* driverIvars = &ivars;
    const kern_return_t error = audioDevice.SetIOOperationHandler(
        ^kern_return_t(IOUserAudioObjectID           objectID,
                       IOUserAudioIOOperation        operation,
                       uint32_t                      ioBufferFrameSize,
                       uint64_t                      sampleTime,
                       uint64_t                      hostTime)
    {
        if (!driverIvars) {
            return kIOReturnNotReady;
        }

        auto* graphControl = driverIvars->runtime.directAudioGraph.control;
        auto& callbackState = graphControl
            ? *graphControl
            : driverIvars->runtime.directAudioControl;

        auto returnError = [&](kern_return_t kr) noexcept {
            callbackState.ioLastError.store(
                static_cast<uint32_t>(kr), std::memory_order_relaxed);
            callbackState.ioLastErrorOperation.store(
                static_cast<uint32_t>(operation), std::memory_order_relaxed);
            callbackState.ioLastErrorFrameCount.store(
                ioBufferFrameSize, std::memory_order_relaxed);
            callbackState.ioLastErrorObjectId.store(
                objectID, std::memory_order_relaxed);
            callbackState.ioLastErrorSampleTime.store(
                sampleTime, std::memory_order_relaxed);
            callbackState.ioLastErrorHostTime.store(
                hostTime, std::memory_order_relaxed);
            callbackState.ioCallbackErrorGeneration.fetch_add(
                1, std::memory_order_release);
            return kr;
        };

        (void)driverIvars->runtime.ioDebugCallbacks.fetch_add(1, std::memory_order_relaxed);
        callbackState.ioLastOperation.store(
            static_cast<uint32_t>(operation), std::memory_order_relaxed);
        callbackState.ioLastFrameCount.store(
            ioBufferFrameSize, std::memory_order_relaxed);
        callbackState.ioLastObjectId.store(
            objectID, std::memory_order_relaxed);
        callbackState.ioLastSampleTime.store(
            sampleTime, std::memory_order_relaxed);
        callbackState.ioLastHostTime.store(
            hostTime, std::memory_order_relaxed);
        callbackState.ioCallbackGeneration.fetch_add(
            1, std::memory_order_release);

        const bool running = driverIvars->runtime.isRunning.load(std::memory_order_acquire);
        const bool skeletonBound =
            driverIvars->runtime.directAudioSkeletonBound.load(std::memory_order_acquire);

        if (gIoCallbackLogBudget.load(std::memory_order_relaxed) != 0) {
            gIoCallbackLogBudget.fetch_sub(1, std::memory_order_relaxed);
            ASFW_LOG(DirectAudio,
                     "[IoCall] op=%u frames=%u sampleTime=%llu running=%d "
                     "skeletonBound=%d control=%d inCap=%u",
                     static_cast<uint32_t>(operation), ioBufferFrameSize,
                     sampleTime, running ? 1 : 0, skeletonBound ? 1 : 0,
                     graphControl != nullptr ? 1 : 0,
                     driverIvars->runtime.directAudioGraph.memory.inputFrameCapacity);
        }

        if (!running) {
            driverIvars->runtime.ioCallbacksOutsideRun.fetch_add(
                1, std::memory_order_relaxed);
            return kIOReturnSuccess;
        }

        if (skeletonBound) {
            auto* control = graphControl;
            if (!control) {
                return returnError(kIOReturnNotReady);
            }

            if (operation == IOUserAudioIOOperationBeginRead) {
                // ADK permits operation spans that differ from the nominal IO
                // size. The stream ring capacity is the actual hard bound.
                if (ioBufferFrameSize >
                    driverIvars->runtime.directAudioGraph.memory.inputFrameCapacity) {
                    return returnError(kIOReturnBadArgument);
                }
                control->client.PublishBeginRead(sampleTime, hostTime, ioBufferFrameSize);
                control->rxCaptureBufferTelemetry.RecordReaderBeginRead();
                (void)PrepareCaptureRingForBeginRead(driverIvars->runtime.directAudioGraph,
                                                     *control,
                                                     sampleTime,
                                                     ioBufferFrameSize);
                control->counters.CountBeginRead();
            } else if (operation == IOUserAudioIOOperationWriteEnd) {
                // See BeginRead above: CoreAudio may choose a larger span than
                // kHalIoPeriodFrames while remaining within the stream ring.
                if (ioBufferFrameSize >
                    driverIvars->runtime.directAudioGraph.memory.outputFrameCapacity) {
                    return returnError(kIOReturnBadArgument);
                }
                const auto& memory =
                    driverIvars->runtime.directAudioGraph.memory;
                const auto stageResult =
                    driverIvars->runtime.txPcmStagingRing.Stage({
                        .interleavedFloat32 = memory.outputBase,
                        .firstFrame = sampleTime,
                        .frameCount = ioBufferFrameSize,
                        .frameCapacity = memory.outputFrameCapacity,
                        .channels = memory.outputChannels,
                    });
                if (stageResult ==
                        ASFW::Audio::Runtime::TxPcmStageResult::kInvalidView ||
                    stageResult ==
                        ASFW::Audio::Runtime::TxPcmStageResult::kNotConfigured) {
                    return returnError(kIOReturnNotReady);
                }
                if (stageResult ==
                    ASFW::Audio::Runtime::TxPcmStageResult::kDuplicate) {
                    // A retried/out-of-order callback must not move W backward
                    // or reinterpret an old HAL span as newly writable PCM.
                    control->counters.CountWriteEnd();
                    return kIOReturnSuccess;
                }

                // Publish W only after the complete callback range has been
                // copied into durable staging. An acquire-reader that observes
                // this frontier can therefore always snapshot every frame below
                // it unless the bounded staging ring explicitly reports stale.
                control->client.PublishWriteEnd(
                    sampleTime, hostTime, ioBufferFrameSize);
                PublishPlaybackRingWriteEnd(
                    driverIvars->runtime.directAudioGraph, *control);

                // Keep packet preparation driven by the CoreAudio write
                // frontier as well as the OHCI refill path. The target is a
                // completed host-write frontier, not a request for transport
                // to manipulate audio cursors. Future PCM does not exist and
                // must never be represented by release-committed zero-filled
                // DATA placeholders. The
                // coalescing latch ensures this RT callback produces at most
                // one outstanding action.
                const uint64_t writeEndFrame = sampleTime + ioBufferFrameSize;
                const uint64_t targetFrameEnd = writeEndFrame;
                const uint64_t requestGeneration =
                    control->txPreparationRequests.PublishRequest(
                        hostTime, targetFrameEnd);
                if (driverIvars->device.audioNub &&
                    control->txPreparationRequests.TryScheduleWake()) {
                    const kern_return_t requestKr =
                        driverIvars->device.audioNub->RequestTxPreparation(
                            requestGeneration);
                    if (requestKr != kIOReturnSuccess) {
                        control->txPreparationRequests.FinishWake();
                    }
                }

                control->counters.CountWriteEnd();
            } else {
                return returnError(kIOReturnBadArgument);
            }
        } else {
            return returnError(kIOReturnNotReady);
        }

        return kIOReturnSuccess;
    });

    if (error == kIOReturnSuccess) {
        ASFW_LOG(DirectAudio,
                 "ADK IO handler installed deviceId=%u inputStream=%u outputStream=%u",
                 audioDevice.GetObjectID(),
                 ivars.inputStream ? ivars.inputStream->GetObjectID() : 0,
                 ivars.outputStream ? ivars.outputStream->GetObjectID() : 0);
    } else {
        ASFW_LOG(Audio, "ASFWAudioDriver: SetIOOperationHandler failed: 0x%x", error);
    }
    return error;
}

} // namespace ASFW::Audio::DriverKit
