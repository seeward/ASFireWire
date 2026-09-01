// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DuplexIRMReservations.hpp - bounded lifecycle owner for duplex IRM resources

#pragma once

#include "../Protocols/AudioTypes.hpp"
#include "../../Bus/IRM/IRMClient.hpp"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOReturn.h>

#include <array>
#include <atomic>
#include <memory>

namespace ASFW::Audio::Duplex {

/// Why a reservation was refused. kIOReturnNoResources alone covers a bus that
/// is genuinely full, a planner that produced no usable channel, a local table
/// overflow and a lost race with another initiator; those need different fixes,
/// so the caller is told which one happened.
enum class IsochReserveFailure : uint8_t {
    kNone = 0,
    kNoChannelsAllowed,  ///< the requested channel mask was empty
    kTableFull,          ///< more streams than this direction can track
    kInvalidChannel,     ///< channel outside 0..63
    kBandwidthShort,     ///< the IRM ledger holds fewer units than this stream costs
    kChannelBusy,        ///< every allowed channel is already allocated on the bus
    kLockContention,     ///< the ledger moved under us and retries ran out
    kNoIRM,              ///< no isochronous resource manager in this generation
    kGenerationChanged,  ///< bus reset invalidated the allocation
    kSnapshotFailed,     ///< the ledger could not be read
};

[[nodiscard]] inline const char* ToString(IsochReserveFailure failure) noexcept {
    switch (failure) {
        case IsochReserveFailure::kNone: return "None";
        case IsochReserveFailure::kNoChannelsAllowed: return "NoChannelsAllowed";
        case IsochReserveFailure::kTableFull: return "TableFull";
        case IsochReserveFailure::kInvalidChannel: return "InvalidChannel";
        case IsochReserveFailure::kBandwidthShort: return "BandwidthShort";
        case IsochReserveFailure::kChannelBusy: return "ChannelBusy";
        case IsochReserveFailure::kLockContention: return "LockContention";
        case IsochReserveFailure::kNoIRM: return "NoIRM";
        case IsochReserveFailure::kGenerationChanged: return "GenerationChanged";
        case IsochReserveFailure::kSnapshotFailed: return "SnapshotFailed";
    }
    return "Unknown";
}

/// What one stream costs the IRM ledger. The packet term comes from the stream
/// plan; the overhead term is a property of the bus at the moment of the
/// allocation, not of the stream.
struct IsochBandwidthCharge final {
    uint32_t packetUnits{0};
    uint32_t overheadUnits{0};
    uint8_t gapCount{63};

    [[nodiscard]] constexpr uint32_t Total() const noexcept {
        return packetUnits + overheadUnits;
    }
};

struct IRMReservationResult final {
    kern_return_t status{kIOReturnError};
    uint8_t channel{AudioStreamWireInfo::kInvalidIsoChannel};
    IsochReserveFailure failure{IsochReserveFailure::kNone};
    IsochBandwidthCharge charge{};
    uint32_t availableUnits{0};     ///< ledger reading when the request was refused
    uint64_t refusedChannels{0};    ///< candidates found already allocated
};

class DuplexIRMReservations final {
  public:
    DuplexIRMReservations() = default;
    ~DuplexIRMReservations() { ReleaseAll(); }

    DuplexIRMReservations(const DuplexIRMReservations&) = delete;
    DuplexIRMReservations& operator=(const DuplexIRMReservations&) = delete;

    [[nodiscard]] kern_return_t Reserve(IRM::IRMClient& client, uint8_t channel,
                                        uint32_t packetBandwidthUnits) noexcept {
        return MapStatus(ReserveSpecific(client, channel,
                                         ChargeFor(client, packetBandwidthUnits)));
    }

    [[nodiscard]] IRMReservationResult ReserveAny(IRM::IRMClient& client,
                                                  uint64_t allowedChannels,
                                                  uint32_t packetBandwidthUnits) noexcept {
        // The overhead half of the charge is a property of the bus, and the bus
        // manager can optimise the gap count between planning and reserving, so
        // it is read here rather than carried in from the plan.
        const IsochBandwidthCharge charge = ChargeFor(client, packetBandwidthUnits);
        const auto refuse = [charge](IsochReserveFailure failure, kern_return_t status,
                                     uint32_t available = 0,
                                     uint64_t refused = 0) noexcept {
            return IRMReservationResult{.status = status,
                                        .channel = AudioStreamWireInfo::kInvalidIsoChannel,
                                        .failure = failure,
                                        .charge = charge,
                                        .availableUnits = available,
                                        .refusedChannels = refused};
        };

        if (count_ >= entries_.size()) {
            return refuse(IsochReserveFailure::kTableFull, kIOReturnNoResources);
        }
        if (allowedChannels == 0) {
            return refuse(IsochReserveFailure::kNoChannelsAllowed, kIOReturnNoResources);
        }

        uint64_t candidates = allowedChannels;
        uint32_t available = 0;
        bool lostARace = false;
        while (candidates != 0) {
            auto snapshotState = std::make_shared<SnapshotWaitState>();
            client.ReadResourcesSnapshot(
                [snapshotState](IRM::AllocationStatus status, IRM::ResourceSnapshot snapshot) {
                    snapshotState->snapshot = snapshot;
                    snapshotState->status.store(status, std::memory_order_release);
                    snapshotState->done.store(true, std::memory_order_release);
                });
            const IRM::AllocationStatus snapshotStatus = WaitSnapshot(snapshotState);
            if (snapshotStatus != IRM::AllocationStatus::Success) {
                return refuse(FailureForStatus(snapshotStatus, IsochReserveFailure::kSnapshotFailed),
                              MapStatus(snapshotStatus));
            }
            available = snapshotState->snapshot.bandwidthAvailable;
            if (available < charge.Total()) {
                return refuse(IsochReserveFailure::kBandwidthShort, kIOReturnNoResources,
                              available);
            }

            const uint8_t channel = FirstAvailableChannel(snapshotState->snapshot, candidates);
            if (channel == AudioStreamWireInfo::kInvalidIsoChannel) {
                return refuse(IsochReserveFailure::kChannelBusy, kIOReturnNoResources, available,
                              allowedChannels);
            }

            // A competing initiator can consume the selected channel between
            // the snapshot and CAS. Exclude that candidate and continue only
            // for a definite no-resource result; timeouts and generation
            // changes have indeterminate/different ownership semantics.
            candidates &= ~(uint64_t{1} << channel);
            const IRM::AllocationStatus status = ReserveSpecific(client, channel, charge);
            if (status == IRM::AllocationStatus::Success) {
                return {.status = kIOReturnSuccess, .channel = channel, .charge = charge,
                        .availableUnits = available};
            }
            // The ledger said this channel was free and the allocation still
            // failed: another initiator took it in between. Try the next
            // candidate. Anything else — a short ledger above all — refuses
            // every candidate equally, so stop and report it.
            if (status != IRM::AllocationStatus::ChannelBusy &&
                status != IRM::AllocationStatus::NoResources) {
                return refuse(FailureForStatus(status, IsochReserveFailure::kSnapshotFailed),
                              MapStatus(status), available);
            }
            lostARace = lostARace || status == IRM::AllocationStatus::NoResources;
        }

        // Every allowed channel looked free in the ledger and was refused when
        // asked for. Distinguish "the bus owns them all" from "we kept losing
        // the compare-swap", and report the last ledger reading either way.
        return refuse(lostARace ? IsochReserveFailure::kLockContention
                                : IsochReserveFailure::kChannelBusy,
                      kIOReturnNoResources, available, allowedChannels);
    }

    void ReleaseAll() noexcept {
        while (count_ > 0) {
            Entry& entry = entries_[--count_];
            if (entry.client == nullptr) {
                entry = {};
                continue;
            }

            auto state = std::make_shared<WaitState>();
            entry.client->ReleaseResources(
                entry.channel, entry.bandwidthUnits, [state](IRM::AllocationStatus status) {
                    state->status.store(status, std::memory_order_release);
                    state->done.store(true, std::memory_order_release);
                });
            (void)Wait(state); // teardown release is best effort, but bounded
            entry = {};
        }
    }

    // An IRM allocation belongs to the bus generation in which it was made.
    // Once that generation is invalid (including provider removal), a release
    // transaction is both unnecessary and unable to complete. Apple
    // IOFWIsochChannel.cpp:1243-1269 likewise clears local allocation state
    // after a bus reset instead of releasing resources on the old generation.
    void InvalidateAfterGenerationChange() noexcept {
        while (count_ > 0) {
            entries_[--count_] = {};
        }
    }

    [[nodiscard]] size_t Count() const noexcept { return count_; }

  private:
    /// One stream's charge against the live bus. Linux re-reads the overhead on
    /// every allocation and reallocation for the same reason
    /// (sound/firewire/iso-resources.c:119,178): it belongs to the bus, and the
    /// bus changes underneath a long-lived plan.
    [[nodiscard]] static IsochBandwidthCharge ChargeFor(const IRM::IRMClient& client,
                                                        uint32_t packetUnits) noexcept {
        const uint8_t gapCount = client.CurrentGapCount();
        return IsochBandwidthCharge{
            .packetUnits = packetUnits,
            .overheadUnits = IRM::BandwidthOverheadForGapCount(gapCount),
            .gapCount = gapCount,
        };
    }

    [[nodiscard]] static IsochReserveFailure
    FailureForStatus(IRM::AllocationStatus status, IsochReserveFailure fallback) noexcept {
        switch (status) {
            case IRM::AllocationStatus::NoIRM:
                return IsochReserveFailure::kNoIRM;
            case IRM::AllocationStatus::GenerationMismatch:
                return IsochReserveFailure::kGenerationChanged;
            case IRM::AllocationStatus::ChannelBusy:
                return IsochReserveFailure::kChannelBusy;
            case IRM::AllocationStatus::BandwidthShort:
                return IsochReserveFailure::kBandwidthShort;
            case IRM::AllocationStatus::NoResources:
                return IsochReserveFailure::kLockContention;
            default:
                return fallback;
        }
    }

    [[nodiscard]] IRM::AllocationStatus ReserveSpecific(IRM::IRMClient& client, uint8_t channel,
                                                        IsochBandwidthCharge charge) noexcept {
        if (count_ >= entries_.size() || channel > 63) {
            return IRM::AllocationStatus::NoResources;
        }

        // Linux cmp.c:188-209 and iso-resources.c:91-147 reserve channel and
        // bandwidth as one lifecycle-owned resource before establishing a PCR.
        // The entry records the charged total, because the release has to hand
        // back exactly what was taken even if the gap count has moved since.
        auto state = std::make_shared<WaitState>();
        client.AllocateResources(channel, charge.Total(), [state](IRM::AllocationStatus status) {
            state->status.store(status, std::memory_order_release);
            state->done.store(true, std::memory_order_release);
        });
        const IRM::AllocationStatus status = Wait(state);
        if (status != IRM::AllocationStatus::Success) {
            return status;
        }

        entries_[count_++] = Entry{
            .client = &client,
            .channel = channel,
            .bandwidthUnits = charge.Total(),
        };
        return IRM::AllocationStatus::Success;
    }
    static constexpr uint32_t kWaitTimeoutMs = 12000;
    static constexpr uint32_t kWaitPollMs = 5;

    struct Entry {
        IRM::IRMClient* client{nullptr};
        uint8_t channel{0};
        uint32_t bandwidthUnits{0};
    };

    struct WaitState {
        std::atomic<bool> done{false};
        std::atomic<IRM::AllocationStatus> status{IRM::AllocationStatus::Failed};
    };

    struct SnapshotWaitState {
        std::atomic<bool> done{false};
        std::atomic<IRM::AllocationStatus> status{IRM::AllocationStatus::Failed};
        IRM::ResourceSnapshot snapshot{};
    };

    [[nodiscard]] static IRM::AllocationStatus
    Wait(const std::shared_ptr<WaitState>& state) noexcept {
        for (uint32_t waited = 0; waited < kWaitTimeoutMs; waited += kWaitPollMs) {
            if (state->done.load(std::memory_order_acquire)) {
                return state->status.load(std::memory_order_acquire);
            }
            IOSleep(kWaitPollMs);
        }
        return IRM::AllocationStatus::Timeout;
    }

    [[nodiscard]] static IRM::AllocationStatus
    WaitSnapshot(const std::shared_ptr<SnapshotWaitState>& state) noexcept {
        for (uint32_t waited = 0; waited < kWaitTimeoutMs; waited += kWaitPollMs) {
            if (state->done.load(std::memory_order_acquire)) {
                return state->status.load(std::memory_order_acquire);
            }
            IOSleep(kWaitPollMs);
        }
        return IRM::AllocationStatus::Timeout;
    }

    [[nodiscard]] static bool IsAvailable(const IRM::ResourceSnapshot& snapshot,
                                          uint8_t channel) noexcept {
        const uint32_t registerValue = channel < 32 ? snapshot.channelsAvailable31_0
                                                    : snapshot.channelsAvailable63_32;
        const uint32_t bit = uint32_t{1} << (31U - (channel % 32U));
        return (registerValue & bit) != 0;
    }

    [[nodiscard]] static uint8_t FirstAvailableChannel(const IRM::ResourceSnapshot& snapshot,
                                                       uint64_t candidates) noexcept {
        for (uint8_t channel = 0; channel < 64; ++channel) {
            if ((candidates & (uint64_t{1} << channel)) != 0 &&
                IsAvailable(snapshot, channel)) {
                return channel;
            }
        }
        return AudioStreamWireInfo::kInvalidIsoChannel;
    }

    [[nodiscard]] static kern_return_t MapStatus(IRM::AllocationStatus status) noexcept {
        switch (status) {
            case IRM::AllocationStatus::Success:
                return kIOReturnSuccess;
            case IRM::AllocationStatus::NoResources:
            case IRM::AllocationStatus::ChannelBusy:
            case IRM::AllocationStatus::BandwidthShort:
                return kIOReturnNoResources;
            case IRM::AllocationStatus::GenerationMismatch:
                return kIOReturnOffline;
            case IRM::AllocationStatus::Timeout:
                return kIOReturnTimeout;
            case IRM::AllocationStatus::NoIRM:
                return kIOReturnNotReady;
            case IRM::AllocationStatus::Failed:
                return kIOReturnError;
        }
        return kIOReturnError;
    }

    // One instance owns one direction, so the bound is independently enforced
    // for playback and capture by IsochDuplexHostTransport's two instances.
    std::array<Entry, kMaxAudioStreamsPerDirection> entries_{};
    size_t count_{0};
};

class DuplexIRMReservationPair final {
  public:
    [[nodiscard]] kern_return_t ReservePlayback(IRM::IRMClient& client, uint8_t channel,
                                                uint32_t bandwidthUnits) noexcept {
        const kern_return_t status = playback_.Reserve(client, channel, bandwidthUnits);
        if (status != kIOReturnSuccess) {
            ReleaseAll();
        }
        return status;
    }

    [[nodiscard]] kern_return_t ReserveCapture(IRM::IRMClient& client, uint8_t channel,
                                               uint32_t bandwidthUnits) noexcept {
        const kern_return_t status = capture_.Reserve(client, channel, bandwidthUnits);
        if (status != kIOReturnSuccess) {
            ReleaseAll();
        }
        return status;
    }

    [[nodiscard]] IRMReservationResult ReserveAnyPlayback(IRM::IRMClient& client,
                                                          uint64_t allowedChannels,
                                                          uint32_t bandwidthUnits) noexcept {
        const IRMReservationResult result =
            playback_.ReserveAny(client, allowedChannels, bandwidthUnits);
        if (result.status != kIOReturnSuccess) {
            ReleaseAll();
        }
        return result;
    }

    [[nodiscard]] IRMReservationResult ReserveAnyCapture(IRM::IRMClient& client,
                                                         uint64_t allowedChannels,
                                                         uint32_t bandwidthUnits) noexcept {
        const IRMReservationResult result =
            capture_.ReserveAny(client, allowedChannels, bandwidthUnits);
        if (result.status != kIOReturnSuccess) {
            ReleaseAll();
        }
        return result;
    }

    void ReleaseAll() noexcept {
        capture_.ReleaseAll();
        playback_.ReleaseAll();
    }

    void InvalidateAfterGenerationChange() noexcept {
        capture_.InvalidateAfterGenerationChange();
        playback_.InvalidateAfterGenerationChange();
    }

    [[nodiscard]] size_t PlaybackCount() const noexcept { return playback_.Count(); }
    [[nodiscard]] size_t CaptureCount() const noexcept { return capture_.Count(); }

  private:
    DuplexIRMReservations playback_{};
    DuplexIRMReservations capture_{};
};

} // namespace ASFW::Audio::Duplex
