#pragma once

#include <cstddef>
#include <cstdint>

namespace ASFW::Lab {

// Native little-endian, same-host wire format consumed by the lab host. Keep
// the layout assertions here and mirror the offsets in
// Host/Services/ADKConfigClient.swift.
constexpr uint32_t kADKConfigLogMagic = 0x4C434647; // 'LCFG'
constexpr uint32_t kADKConfigWireVersion = 2;
constexpr uint32_t kADKConfigDeviceCount = 4;
// Keep the IOUserClient structure result below 4 KiB: the live control plane
// rejected a 96-event (5440-byte) result, while this cap produces 3648 bytes.
constexpr uint32_t kADKConfigLogMaxEvents = 64;
constexpr uint32_t kADKConfigLogDefaultEvents = 32;
constexpr uint32_t kADKConfigRateA = 44100;
constexpr uint32_t kADKConfigRateB = 48000;

// Native diagnostic control-plane encoding. It deliberately mirrors the host
// bridge's optical values without importing a host-only interface into the
// dext. None is valid only for devices without an optical interface.
enum class ADKConfigOpticalMode : uint32_t {
    None = 0,
    Adat = 1,
    Spdif = 2,
};

[[nodiscard]] constexpr uint32_t PackADKConfigChannelCounts(
    uint32_t oldChannels, uint32_t newChannels) noexcept
{
    return (oldChannels & 0xFFFFu) | ((newChannels & 0xFFFFu) << 16u);
}
static_assert(PackADKConfigChannelCounts(8, 16) == 0x00100008u);

constexpr uint32_t kLabDiagSelectorRequestSampleRate = 1;
constexpr uint32_t kLabDiagSelectorCopyConfigLog = 2;
constexpr uint32_t kLabDiagSelectorCopyConfigState = 3;
constexpr uint32_t kLabDiagSelectorRequestConfiguration = 4;
constexpr uint32_t kLabDiagSelectorSetHardwareOutcome = 5;

enum class ADKConfigHardwareOutcome : uint32_t {
    ConfirmRequested = 0,
    Unchanged = 1,
    Unknown = 2,
};

enum class ADKConfigPhase : uint32_t {
    HostRequest = 1,
    RequestCalled = 2,
    RequestReturned = 3,
    RequestRejected = 4,
    StopIOEnter = 5,
    StopIOReturn = 6,
    PerformEnter = 7,
    PerformMutation = 8,
    PerformSuper = 9,
    PerformReturn = 10,
    AbortEnter = 11,
    AbortSuper = 12,
    AbortReturn = 13,
    StartIOEnter = 14,
    StartIOReturn = 15,
    HandleSampleRateEnter = 16,
    HandleSampleRateReturn = 17,
    DeviceRateMutation = 18,
    OutputStreamMutation = 19,
    InputStreamMutation = 20,
    CandidateAccepted = 21,
    CandidateRejected = 22,
    HardwareApply = 23,
    HardwareCompleted = 24,
    HardwareUnchanged = 25,
    HardwareUnknown = 26,
    ProjectionCommitted = 27,
    CoordinatorRejected = 28,
};

struct ADKConfigLogHeader final {
    uint32_t magic{kADKConfigLogMagic};
    uint32_t version{kADKConfigWireVersion};
    uint32_t eventCount{0};
    uint32_t eventStride{0};
    uint64_t oldestSequence{0};
    uint64_t newestSequence{0};
    uint64_t hostTimeTicks{0};
    uint32_t deviceSlot{0};
    uint32_t deviceObjectID{0};
    uint32_t currentSampleRate{0};
    uint32_t pendingSampleRate{0};
    uint32_t configurationPending{0};
    uint32_t reserved{0};
};
static_assert(sizeof(ADKConfigLogHeader) == 64,
              "ADK configuration log header layout is a wire contract");

struct ADKConfigEvent final {
    uint64_t sequence{0};
    uint64_t hostTimeTicks{0};
    uint32_t deviceSlot{0};
    uint32_t deviceObjectID{0};
    uint64_t action{0};
    uint32_t phase{0};
    int32_t result{0};
    uint32_t oldSampleRate{0};
    uint32_t newSampleRate{0};
    uint32_t configurationPending{0};
    // Low 16 bits: old stream channel count; high 16 bits: new count.
    // Zero for phases that do not describe a stream-format mutation.
    uint32_t streamChannelCounts{0};
};
static_assert(sizeof(ADKConfigEvent) == 56,
              "ADK configuration event layout is a wire contract");

struct ADKConfigState final {
    uint32_t magic{kADKConfigLogMagic};
    uint32_t version{kADKConfigWireVersion};
    uint32_t byteSize{sizeof(ADKConfigState)};
    uint32_t deviceSlot{0};
    uint32_t deviceObjectID{0};
    uint32_t currentSampleRate{0};
    uint32_t pendingSampleRate{0};
    uint32_t configurationPending{0};
    uint32_t currentOpticalInput{0};
    uint32_t currentOpticalOutput{0};
    uint32_t pendingOpticalInput{0};
    uint32_t pendingOpticalOutput{0};
    uint32_t currentInputChannels{0};
    uint32_t currentOutputChannels{0};
    uint64_t pendingAction{0};
    uint64_t nextSequence{0};
};
static_assert(sizeof(ADKConfigState) == 72,
              "ADK configuration state layout is a wire contract");

[[nodiscard]] constexpr size_t ADKConfigLogBlobSize(uint32_t eventCount) noexcept {
    return sizeof(ADKConfigLogHeader) +
           static_cast<size_t>(eventCount) * sizeof(ADKConfigEvent);
}
static_assert(ADKConfigLogBlobSize(kADKConfigLogMaxEvents) == 3648);

} // namespace ASFW::Lab
