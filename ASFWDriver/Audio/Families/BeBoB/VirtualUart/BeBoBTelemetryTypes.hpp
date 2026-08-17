// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBTelemetryTypes.hpp — Strongly-typed telemetry models for BeBoB diagnostics.
//
// Represents device-side metrics from BridgeCo DM1000/DM1500 firmware:
// - Streaming statistics (from `sys stat` / sub_200E1E64)
// - Silicon hardware lock status (from `sys avstat` / sub_201222F8)
// - Master audio & clock sync state (from `fw sync show` / sub_2011EC00)

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

/// Audio engine lifecycle state from firmware state machine (sub_2011EC00).
enum class BeBoBAudioState : uint8_t {
    kStop = 0,
    kIdle = 1,
    kWaitingForSync = 2,
    kRunning = 3,
    kUnknown = 255
};

[[nodiscard]] constexpr const char* ToString(BeBoBAudioState state) noexcept {
    switch (state) {
    case BeBoBAudioState::kStop: return "Stop";
    case BeBoBAudioState::kIdle: return "Idle";
    case BeBoBAudioState::kWaitingForSync: return "Waiting for sync";
    case BeBoBAudioState::kRunning: return "Running";
    default: return "Unknown";
    }
}

/// Clock sync source from firmware (sub_2011EC00).
enum class BeBoBSyncSource : uint8_t {
    kInternal = 0,
    kInternalDigitalInput = 1,
    kAdatExternal = 2,
    kSpdifExternal = 3,
    kWordClock = 4,
    kUnknown = 255
};

[[nodiscard]] constexpr const char* ToString(BeBoBSyncSource source) noexcept {
    switch (source) {
    case BeBoBSyncSource::kInternal: return "Internal Sync";
    case BeBoBSyncSource::kInternalDigitalInput: return "Internal Digital Input Sync";
    case BeBoBSyncSource::kAdatExternal: return "Adat External Sync";
    case BeBoBSyncSource::kSpdifExternal: return "Spdif External Sync";
    case BeBoBSyncSource::kWordClock: return "Word Clock Sync";
    default: return "Unknown";
    }
}

/// Master sync state returned by `fw sync show` or `StreamSync_PrintState`.
struct BeBoBSyncState final {
    BeBoBAudioState audioState{BeBoBAudioState::kUnknown};
    BeBoBSyncSource syncSource{BeBoBSyncSource::kUnknown};
    uint32_t sampleRateHz{0};
    uint8_t lineInChannels{0};
    uint8_t spdifAdatInChannels{0};
    uint8_t spdifAdatOutChannels{0};
    uint8_t mixerOutChannels{0};
};

/// Hardware silicon status bits from DM1000 Framer / TGEN (sub_201222F8).
struct BeBoBAvStat final {
    bool setTgInLock{false};       // TGEN timing generator PLL locked
    bool setTgSytMiss{false};      // TGEN missed expected SYT packet
    bool cipMismatch{false};       // Framer rejected CIP header format
    bool dbcMismatch{false};       // Framer detected DBC continuity error
    bool headerMismatch{false};    // Isochronous packet length/CRC mismatch
};

/// Live streaming metrics from `sys stat` (sub_200E1E64).
struct BeBoBStreamingStats final {
    // Input Stream (Host Transmit -> Device Receive)
    uint64_t rxPackets{0};         // Total isochronous packets received
    uint64_t onlyHeaders{0};       // Empty NO-DATA CIP packets
    uint64_t rxEmptyPkt{0};
    uint64_t rxNoMem{0};
    uint64_t rxToLong{0};
    uint64_t rxPktToLong{0};
    uint64_t rxPktToSmall{0};
    uint64_t rxDmaBusy{0};
    uint64_t rxQFull{0};
    uint32_t rxQFillLevelPct{0};   // 0% = starving, 100% = overflowing
    uint32_t poolFillLevelPct{0};

    // Protocol & Cadence Errors
    uint64_t ctrDiffErr{0};        // Cycle timer counter drift
    uint64_t sytDiffErr{0};        // SYT presentation timestamp out of window
    uint64_t sumDiffErr{0};
    uint64_t bcoHdrErr{0};         // BridgeCo header structure corruption

    // SYT Window Classifiers & Offsets
    uint64_t pktFuture{0};         // Packets timestamped too far ahead (lead too high)
    uint64_t pktPast{0};           // Packets arriving too late (lead too low)
    int32_t pktSytDiff{0};         // Instantaneous signed tick error
    uint32_t sytOffset{0};         // Expected presentation offset (ticks)
    int32_t sytCorr{0};            // Dynamic PLL correction applied (ticks)
    uint32_t linStartSyt{0};       // Initial start timestamp
    uint32_t outStartSyt{0};

    // Global ISR counters
    uint64_t rxIsr{0};
    uint64_t txIsr{0};
    uint64_t mdbAliveErrors{0};
    uint64_t distortErrors{0};
};

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
