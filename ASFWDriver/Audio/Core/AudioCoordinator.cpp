// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "AudioCoordinator.hpp"

#include "AudioEndpointRuntime.hpp"
#include "AudioRuntimeRegistry.hpp"
#include "../Duplex/SyncAsyncBridge.hpp"
#include "../Protocols/IDeviceProtocol.hpp"
#include <net.mrmidi.ASFW.ASFWDriver/ASFWAudioNub.h>
#include "../../Logging/Logging.hpp"

#include <utility>

namespace ASFW::Audio {

AudioCoordinator::AudioCoordinator(IOService* driver,
                                   Discovery::DeviceRegistry& registry,
                                   AudioRuntimeRegistry& runtime,
                                   Driver::IsochService& isoch,
                                   Driver::HardwareInterface& hardware) noexcept
    : publisher_(driver)
    , runtime_(runtime)
    , hostTransport_(isoch)
    , duplexCoordinator_(
          registry, runtime_, hostTransport_, hardware, &teardownRequested_,
          [this](EndpointId endpointId)
              -> Runtime::IDirectAudioBindingSource* {
              auto endpoint = runtime_.FindEndpointRuntime(endpointId);
              return endpoint ? endpoint.get() : nullptr;
          }) {
    lock_ = IOLockAlloc();
    if (!lock_) {
        ASFW_LOG_ERROR(Audio, "AudioCoordinator: lock allocation failed");
    }
    hostTransport_.SetTimingLossCallback(
        [this](EndpointId endpointId) {
            HandleHostTimingLoss(endpointId);
        });
}

AudioCoordinator::~AudioCoordinator() noexcept {
    BeginTeardown();
    if (lock_) {
        IOLockFree(lock_);
        lock_ = nullptr;
    }
}

void AudioCoordinator::SetTxPreparationCallback(
    Driver::IsochService::TxPreparationCallback callback) noexcept {
    hostTransport_.SetTxPreparationCallback(std::move(callback));
}

void AudioCoordinator::SetClockAnchorReadyCallback(
    IsochDuplexHostTransport::ClockAnchorReadyCallback callback) noexcept {
    hostTransport_.SetClockAnchorReadyCallback(std::move(callback));
}

void AudioCoordinator::SetSessionStreamingCallback(
    SessionStreamingCallback callback) noexcept {
    if (!lock_) return;
    IOLockLock(lock_);
    sessionStreamingCallback_ = std::move(callback);
    IOLockUnlock(lock_);
}

void AudioCoordinator::EndpointReady(
    std::shared_ptr<const Devices::ResolvedAudioEndpointProfile> profile,
    std::shared_ptr<IDeviceProtocol> protocolHold) noexcept {
    if (!profile || !profile->endpointId || teardownRequested_.load()) return;

    auto runtime = runtime_.InsertResolved(profile, std::move(protocolHold));
    if (!runtime) {
        ASFW_LOG_ERROR(Audio,
                       "[AudioSession] endpoint=%llu runtime installation failed",
                       profile->endpointId.value);
        return;
    }
    duplexCoordinator_.AcknowledgeDevicePresent(profile->endpointId);
    if (lock_) {
        IOLockLock(lock_);
        invalidatedEndpoints_.erase(profile->endpointId);
        IOLockUnlock(lock_);
    }

    if (!publisher_.EnsureNub(*profile, "resolved-endpoint")) {
        runtime_.Remove(profile->endpointId);
        duplexCoordinator_.CancelRemoteDevice(profile->endpointId);
        ASFW_LOG_ERROR(Audio,
                       "[AudioSession] endpoint=%llu nub publication failed",
                       profile->endpointId.value);
        return;
    }
    ASFW_LOG(Audio,
             "[AudioSession] endpoint=%llu provider=%u published instance=%llu observedGUID=%llx",
             profile->endpointId.value,
             static_cast<unsigned>(profile->familyProvider),
             profile->deviceInstanceId.value,
             profile->observedGuid);
}

void AudioCoordinator::QuiesceEndpoint(EndpointId endpointId) noexcept {
    if (!endpointId) return;
    bool wasActive = false;
    if (lock_) {
        IOLockLock(lock_);
        wasActive = activeEndpoint_ == endpointId;
        if (wasActive) activeEndpoint_ = {};
        IOLockUnlock(lock_);
    }
    if (wasActive) {
        const IOReturn status = duplexCoordinator_.StopStreaming(endpointId);
        if (status != kIOReturnSuccess && status != kIOReturnNoDevice &&
            status != kIOReturnNotReady) {
            ASFW_LOG_WARNING(Audio,
                             "[AudioSession] endpoint=%llu quiesce status=0x%x",
                             endpointId.value, status);
        }
        (void)StopHostTransport("endpoint-quiesce", true);
    }
    if (auto endpoint = runtime_.FindEndpointRuntime(endpointId)) {
        endpoint->MarkStreaming(false);
    }
    (void)NotifySessionStreaming(endpointId, false);
}

void AudioCoordinator::InvalidateEndpointBindings(EndpointId endpointId) noexcept {
    if (!endpointId) return;
    duplexCoordinator_.CancelRemoteDevice(endpointId);
    runtime_.Remove(endpointId);
    if (lock_) {
        IOLockLock(lock_);
        invalidatedEndpoints_.insert(endpointId);
        IOLockUnlock(lock_);
    }
}

void AudioCoordinator::TerminateEndpoint(EndpointId endpointId) noexcept {
    publisher_.TerminateNub(endpointId, "session-retired");
    duplexCoordinator_.ClearSession(endpointId);
}

IOReturn AudioCoordinator::StartStreaming(EndpointId endpointId) noexcept {
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    if (!runtime_.FindProfile(endpointId)) return kIOReturnNoDevice;

    bool alreadyActive = false;
    if (lock_) {
        IOLockLock(lock_);
        if (invalidatedEndpoints_.contains(endpointId)) {
            IOLockUnlock(lock_);
            return kIOReturnNoDevice;
        }
        if (!activeEndpoint_) {
            activeEndpoint_ = endpointId;
        } else if (activeEndpoint_ == endpointId) {
            alreadyActive = true;
        } else {
            IOLockUnlock(lock_);
            return kIOReturnBusy;
        }
        IOLockUnlock(lock_);
    }

    if (alreadyActive) {
        return NotifySessionStreaming(endpointId, true)
            ? kIOReturnSuccess
            : kIOReturnNoDevice;
    }

    const IOReturn status = duplexCoordinator_.StartStreaming(endpointId);
    if (status != kIOReturnSuccess) {
        if (lock_) {
            IOLockLock(lock_);
            if (activeEndpoint_ == endpointId) activeEndpoint_ = {};
            IOLockUnlock(lock_);
        }
        return status;
    }
    if (auto endpoint = runtime_.FindEndpointRuntime(endpointId)) {
        endpoint->MarkStreaming(true);
    }
    if (!NotifySessionStreaming(endpointId, true)) {
        (void)duplexCoordinator_.StopStreaming(endpointId);
        if (auto endpoint = runtime_.FindEndpointRuntime(endpointId)) {
            endpoint->MarkStreaming(false);
        }
        if (lock_) {
            IOLockLock(lock_);
            if (activeEndpoint_ == endpointId) activeEndpoint_ = {};
            IOLockUnlock(lock_);
        }
        return kIOReturnNoDevice;
    }
    return kIOReturnSuccess;
}

IOReturn AudioCoordinator::StopStreaming(EndpointId endpointId) noexcept {
    if (!endpointId) return kIOReturnBadArgument;
    if (lock_) {
        IOLockLock(lock_);
        if (invalidatedEndpoints_.contains(endpointId)) {
            IOLockUnlock(lock_);
            return kIOReturnSuccess;
        }
        if (activeEndpoint_ && activeEndpoint_ != endpointId) {
            IOLockUnlock(lock_);
            return kIOReturnBusy;
        }
        IOLockUnlock(lock_);
    }
    const IOReturn status = duplexCoordinator_.StopStreaming(endpointId);
    if (status == kIOReturnSuccess && lock_) {
        IOLockLock(lock_);
        if (activeEndpoint_ == endpointId) activeEndpoint_ = {};
        IOLockUnlock(lock_);
    }
    if (status == kIOReturnSuccess) {
        if (auto endpoint = runtime_.FindEndpointRuntime(endpointId)) {
            endpoint->MarkStreaming(false);
        }
        (void)NotifySessionStreaming(endpointId, false);
    }
    return status;
}

IOReturn AudioCoordinator::RequestClockConfig(
    EndpointId endpointId,
    const AudioClockConfig& desiredClock,
    DuplexRestartReason reason) noexcept {
    if (!endpointId) return kIOReturnBadArgument;
    const auto profile = runtime_.FindProfile(endpointId);
    if (!profile) return kIOReturnNoDevice;
    bool supported = false;
    for (uint8_t i = 0; i < profile->supportedRateCount; ++i) {
        supported |= profile->supportedRates[i] == desiredClock.sampleRateHz;
    }
    if (!supported) return kIOReturnUnsupported;

    const IOReturn status = duplexCoordinator_.RequestClockConfig(
        endpointId, desiredClock, reason);
    if (status == kIOReturnSuccess) {
        if (auto endpoint = runtime_.FindEndpointRuntime(endpointId)) {
            endpoint->SetCurrentSampleRate(desiredClock.sampleRateHz);
        }
    }
    return status;
}

IOReturn AudioCoordinator::ApplyDeviceConfiguration(
    EndpointId endpointId,
    const Configuration::DeviceConfiguration& desired,
    AudioConfigurationApplyResult& outResult) noexcept {
    outResult = {};
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto profile = runtime_.FindProfile(endpointId);
    const auto protocol = runtime_.FindShared(endpointId);
    if (!profile || !protocol) {
        return kIOReturnNoDevice;
    }
    const auto* capability = profile->ConfigurationFor(desired);
    auto* control = protocol->AsAudioConfigurationControl();
    if (!capability || !control || !control->SupportsConfiguration(desired)) {
        ASFW_LOG(Audio,
                 "[AudioConfig] endpoint=%llu rejected unsupported rate=%u opticalIn=%u opticalOut=%u",
                 endpointId.value, desired.sampleRate,
                 desired.opticalInput ? static_cast<unsigned>(*desired.opticalInput) : 0U,
                 desired.opticalOutput ? static_cast<unsigned>(*desired.opticalOutput) : 0U);
        return kIOReturnUnsupported;
    }

    // The FCP completion is delivered independently from the nub dispatch
    // queue. The bridge is bounded and deliberately mirrors the existing
    // clock-config synchronization policy; no ADK object is touched here.
    const auto completed = WaitForAsyncResult<AudioConfigurationApplyResult>(
        [control, desired](IAudioConfigurationControl::ApplyCallback callback) {
            control->ApplyConfiguration(desired, std::move(callback));
        },
        profile->clockPolicy.lockTimeoutMs + 2'000U,
        kIOReturnTimeout,
        &teardownRequested_);
    if (completed.status != kIOReturnSuccess) {
        ASFW_LOG_ERROR(Audio,
                       "[AudioConfig] endpoint=%llu hardware apply failed kr=0x%x",
                       endpointId.value, completed.status);
        return completed.status;
    }
    const auto* confirmedCapability =
        profile->ConfigurationFor(completed.value.configuration);
    if (!confirmedCapability ||
        confirmedCapability->runtimeCaps.hostInputPcmChannels !=
            completed.value.runtimeCaps.hostInputPcmChannels ||
        confirmedCapability->runtimeCaps.hostOutputPcmChannels !=
            completed.value.runtimeCaps.hostOutputPcmChannels) {
        ASFW_LOG_ERROR(Audio,
                       "[AudioConfig] endpoint=%llu hardware returned unadvertised topology",
                       endpointId.value);
        return kIOReturnError;
    }
    outResult = completed.value;
    return kIOReturnSuccess;
}

IOReturn AudioCoordinator::CommitDeviceConfiguration(
    EndpointId endpointId,
    const AudioConfigurationApplyResult& confirmed) noexcept {
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto profile = runtime_.FindProfile(endpointId);
    const auto endpoint = runtime_.FindEndpointRuntime(endpointId);
    const auto* capability = profile ? profile->ConfigurationFor(confirmed.configuration)
                                     : nullptr;
    if (!endpoint || !capability ||
        capability->runtimeCaps.hostInputPcmChannels !=
            confirmed.runtimeCaps.hostInputPcmChannels ||
        capability->runtimeCaps.hostOutputPcmChannels !=
            confirmed.runtimeCaps.hostOutputPcmChannels) {
        return kIOReturnBadArgument;
    }
    return endpoint->ApplyConfiguration(confirmed.runtimeCaps)
        ? kIOReturnSuccess : kIOReturnError;
}

IOReturn AudioCoordinator::RequestDeviceConfiguration(
    EndpointId endpointId,
    const Configuration::DeviceConfiguration& desired) noexcept {
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto profile = runtime_.FindProfile(endpointId);
    auto* nub = publisher_.GetNub(endpointId);
    if (!profile || !nub) return kIOReturnNoDevice;
    if (!profile->ConfigurationFor(desired) || !desired.opticalInput ||
        !desired.opticalOutput) {
        return kIOReturnUnsupported;
    }
    const uint32_t input = *desired.opticalInput == Configuration::OpticalMode::Adat ? 1U : 2U;
    const uint32_t output = *desired.opticalOutput == Configuration::OpticalMode::Adat ? 1U : 2U;
    return nub->NotifyDeviceConfigurationRequested(desired.sampleRate, input, output)
        ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn AudioCoordinator::CopyDeviceConfigurationSnapshot(
    EndpointId endpointId,
    Configuration::DeviceConfigurationSnapshot& outSnapshot) noexcept {
    outSnapshot = {};
    const auto profile = runtime_.FindProfile(endpointId);
    const auto endpoint = runtime_.FindEndpointRuntime(endpointId);
    if (!profile || !endpoint) return kIOReturnNoDevice;

    uint32_t sampleRateHz = 0;
    uint32_t inputChannels = 0;
    uint32_t outputChannels = 0;
    if (!endpoint->CopyActiveConfiguration(sampleRateHz, inputChannels, outputChannels)) {
        return kIOReturnNotReady;
    }
    outSnapshot.endpointId = endpointId.value;
    outSnapshot.inputChannels = inputChannels;
    outSnapshot.outputChannels = outputChannels;
    const uint8_t count = std::min(
        profile->configurationCapabilityCount,
        static_cast<uint8_t>(outSnapshot.capabilities.size()));
    bool foundCommitted = false;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& source = profile->configurationCapabilities[i];
        auto& destination = outSnapshot.capabilities[i];
        destination.configuration = source.configuration;
        destination.inputChannels = source.runtimeCaps.hostInputPcmChannels;
        destination.outputChannels = source.runtimeCaps.hostOutputPcmChannels;
        if (source.configuration.sampleRate == sampleRateHz &&
            destination.inputChannels == inputChannels &&
            destination.outputChannels == outputChannels) {
            outSnapshot.committed = source.configuration;
            foundCommitted = true;
        }
    }
    outSnapshot.capabilityCount = count;
    return foundCommitted ? kIOReturnSuccess : kIOReturnError;
}

uint32_t AudioCoordinator::CopyConfigurationEndpointIds(
    std::array<EndpointId,
               Configuration::kMaxConfigurationSnapshotCapabilities>& out) noexcept {
    return runtime_.CopyConfigurationEndpointIds(out);
}

IOReturn AudioCoordinator::CopyAudioControlSurfaceSnapshot(
    EndpointId endpointId, AudioControlSurfaceSnapshot& outSnapshot) noexcept {
    outSnapshot = {};
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto protocol = runtime_.FindShared(endpointId);
    auto* surface = protocol ? protocol->AsAudioControlSurface() : nullptr;
    if (!surface) return kIOReturnUnsupported;
    return surface->CopyAudioControlSurfaceSnapshot(outSnapshot)
        ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn AudioCoordinator::RequestAudioControlValue(
    EndpointId endpointId, uint32_t controlId, int32_t value) noexcept {
    // Selector 1019 predates the asynchronous control plane. It deliberately
    // remains a hard refusal: waiting for a FireWire completion here can block
    // the UserClient queue for two seconds and make the host appear frozen.
    (void)endpointId;
    (void)controlId;
    (void)value;
    return kIOReturnUnsupported;
}

IOReturn AudioCoordinator::SubmitAudioControlValue(
    EndpointId endpointId, uint32_t controlId, int32_t value,
    IAudioControlSurface::ApplyCallback completion) noexcept {
    if (!completion) return kIOReturnBadArgument;
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto protocol = runtime_.FindShared(endpointId);
    auto* surface = protocol ? protocol->AsAudioControlSurface() : nullptr;
    if (!surface) return kIOReturnUnsupported;
    surface->ApplyAudioControlValue(controlId, value, std::move(completion));
    return kIOReturnSuccess;
}

IOReturn AudioCoordinator::CopyAudioMeterSnapshot(
    EndpointId endpointId, AudioMeterSnapshot& outSnapshot) noexcept {
    outSnapshot = {};
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto protocol = runtime_.FindShared(endpointId);
    auto* metering = protocol ? protocol->AsAudioMetering() : nullptr;
    if (!metering) return kIOReturnUnsupported;
    return metering->CopyAudioMeterSnapshot(outSnapshot)
        ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn AudioCoordinator::SetAudioMeteringEnabled(
    EndpointId endpointId, bool enabled) noexcept {
    if (!endpointId || teardownRequested_.load(std::memory_order_acquire)) {
        return kIOReturnNotReady;
    }
    const auto protocol = runtime_.FindShared(endpointId);
    auto* metering = protocol ? protocol->AsAudioMetering() : nullptr;
    return metering ? metering->SetAudioMeteringEnabled(enabled) : kIOReturnUnsupported;
}

void AudioCoordinator::HandleCycleInconsistent() noexcept {
    EndpointId endpointId{};
    if (lock_) {
        IOLockLock(lock_);
        endpointId = activeEndpoint_;
        IOLockUnlock(lock_);
    }
    if (endpointId) {
        (void)duplexCoordinator_.RecoverStreaming(
            endpointId, DuplexRestartReason::kRecoverAfterCycleInconsistent);
    }
}

void AudioCoordinator::HandleHostTimingLoss(EndpointId endpointId) noexcept {
    if (!endpointId) return;
    if (lock_) {
        IOLockLock(lock_);
        const bool invalidated = invalidatedEndpoints_.contains(endpointId);
        IOLockUnlock(lock_);
        if (invalidated) return;
    }
    (void)duplexCoordinator_.RecoverStreaming(
        endpointId, DuplexRestartReason::kRecoverAfterTimingLoss);
}

void AudioCoordinator::BeginTeardown() noexcept {
    if (teardownRequested_.exchange(true, std::memory_order_acq_rel)) return;
    hostTransport_.SetTimingLossCallback({});
    hostTransport_.SetTxPreparationCallback({});
    hostTransport_.SetClockAnchorReadyCallback({});
    (void)StopHostTransport("service-teardown", false);
    if (lock_) {
        IOLockLock(lock_);
        activeEndpoint_ = {};
        IOLockUnlock(lock_);
    }
}

bool AudioCoordinator::NotifySessionStreaming(EndpointId endpointId,
                                              bool streaming) noexcept {
    SessionStreamingCallback callback;
    if (lock_) {
        IOLockLock(lock_);
        callback = sessionStreamingCallback_;
        IOLockUnlock(lock_);
    }
    // Endpoint publication is session-manager owned. Absence of the callback
    // therefore means there is no authoritative live session to transition.
    return callback && callback(endpointId, streaming);
}

kern_return_t AudioCoordinator::StopHostTransport(
    const char* reason, bool generationInvalidated) noexcept {
    const kern_return_t status = generationInvalidated
        ? hostTransport_.StopAllAfterBusReset()
        : hostTransport_.StopAll();
    ASFW_LOG(Audio,
             "[Lifecycle] AudioCoordinator host teardown reason=%{public}s reset=%u kr=0x%08x",
             reason ? reason : "unknown", generationInvalidated ? 1U : 0U,
             status);
    return status;
}

std::optional<AudioCoordinator::EndpointId>
AudioCoordinator::GetSinglePublishedEndpointId() const noexcept {
    return publisher_.GetSingleEndpointId();
}

} // namespace ASFW::Audio
