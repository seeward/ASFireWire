// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "BeBoBStreamTelemetryParser.hpp"

#include <charconv>
#include <string_view>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

namespace {

/// Helper to extract uint64 after a key substring
[[nodiscard]] std::optional<uint64_t> ExtractUInt64(std::string_view text, std::string_view key) noexcept {
    const auto pos = text.find(key);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto rest = text.substr(pos + key.size());
    // skip whitespace or formatting chars (. : = \t)
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == ':' || rest.front() == '=' || rest.front() == '.')) {
        rest.remove_prefix(1);
    }
    uint64_t val = 0;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), val);
    if (ec == std::errc{}) {
        return val;
    }
    return std::nullopt;
}

/// Helper to extract int32 after a key substring
[[nodiscard]] std::optional<int32_t> ExtractInt32(std::string_view text, std::string_view key) noexcept {
    const auto pos = text.find(key);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto rest = text.substr(pos + key.size());
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == ':' || rest.front() == '=' || rest.front() == '.')) {
        rest.remove_prefix(1);
    }
    int32_t val = 0;
    const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), val);
    if (ec == std::errc{}) {
        return val;
    }
    return std::nullopt;
}

} // namespace

std::optional<BeBoBStreamingStats> BeBoBStreamTelemetryParser::ParseStreamingStats(
    std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    BeBoBStreamingStats stats{};
    bool matchedAny = false;

    if (auto v = ExtractUInt64(text, "rxPackets")) { stats.rxPackets = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "onlyHeaders")) { stats.onlyHeaders = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxEmptyPkt")) { stats.rxEmptyPkt = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxNoMem")) { stats.rxNoMem = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxToLong")) { stats.rxToLong = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxPktToLong")) { stats.rxPktToLong = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxPktToSmall")) { stats.rxPktToSmall = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxDmaBusy")) { stats.rxDmaBusy = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxQFull")) { stats.rxQFull = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "rxQFillLevel")) { stats.rxQFillLevelPct = static_cast<uint32_t>(*v); matchedAny = true; }
    if (auto v = ExtractUInt64(text, "PoolFillLevel")) { stats.poolFillLevelPct = static_cast<uint32_t>(*v); matchedAny = true; }

    if (auto v = ExtractUInt64(text, "CtrDiffErr")) { stats.ctrDiffErr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "SytDiffErr")) { stats.sytDiffErr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "SumDiffErr")) { stats.sumDiffErr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "BCOHdrErr")) { stats.bcoHdrErr = *v; matchedAny = true; }

    if (auto v = ExtractUInt64(text, "pkt Future")) { stats.pktFuture = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "pkt Past")) { stats.pktPast = *v; matchedAny = true; }
    if (auto v = ExtractInt32(text, "pktSytDiff")) { stats.pktSytDiff = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "SytOffset")) { stats.sytOffset = static_cast<uint32_t>(*v); matchedAny = true; }
    if (auto v = ExtractInt32(text, "SytCorr")) { stats.sytCorr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "linStartSyt")) { stats.linStartSyt = static_cast<uint32_t>(*v); matchedAny = true; }
    if (auto v = ExtractUInt64(text, "outStartSyt")) { stats.outStartSyt = static_cast<uint32_t>(*v); matchedAny = true; }

    if (auto v = ExtractUInt64(text, "rxIsr")) { stats.rxIsr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "txIsr")) { stats.txIsr = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "MDBAliveErrors")) { stats.mdbAliveErrors = *v; matchedAny = true; }
    if (auto v = ExtractUInt64(text, "distortErrors")) { stats.distortErrors = *v; matchedAny = true; }

    if (!matchedAny && text.find("Stream statistic") == std::string_view::npos) {
        return std::nullopt;
    }

    return stats;
}

std::optional<BeBoBAvStat> BeBoBStreamTelemetryParser::ParseAvStat(
    std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    BeBoBAvStat stat{};

    // Look for TGEN lock indicator or bitfield outputs
    if (text.find("SetTgInLock") != std::string_view::npos ||
        text.find("TGEN in lock") != std::string_view::npos ||
        text.find("InLock") != std::string_view::npos) {
        stat.setTgInLock = true;
    }
    if (text.find("SetTgSytMiss") != std::string_view::npos ||
        text.find("SytMiss") != std::string_view::npos) {
        stat.setTgSytMiss = true;
    }
    if (auto v = ExtractUInt64(text, "CIPMismatch")) {
        stat.cipMismatch = (*v != 0);
    } else if (text.find("CIPMismatch") != std::string_view::npos) {
        stat.cipMismatch = true;
    }

    if (auto v = ExtractUInt64(text, "DBCMismatch")) {
        stat.dbcMismatch = (*v != 0);
    } else if (text.find("DBCMismatch") != std::string_view::npos) {
        stat.dbcMismatch = true;
    }

    if (auto v = ExtractUInt64(text, "HeaderMismatch")) {
        stat.headerMismatch = (*v != 0);
    } else if (text.find("HeaderMismatch") != std::string_view::npos) {
        stat.headerMismatch = true;
    }

    return stat;
}

std::optional<BeBoBSyncState> BeBoBStreamTelemetryParser::ParseSyncState(
    std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    BeBoBSyncState state{};

    // Audio State
    if (text.find("Waiting for sync") != std::string_view::npos) {
        state.audioState = BeBoBAudioState::kWaitingForSync;
    } else if (text.find("Running") != std::string_view::npos) {
        state.audioState = BeBoBAudioState::kRunning;
    } else if (text.find("Idle") != std::string_view::npos) {
        state.audioState = BeBoBAudioState::kIdle;
    } else if (text.find("Stop") != std::string_view::npos) {
        state.audioState = BeBoBAudioState::kStop;
    }

    // Sync Source
    if (text.find("Internal Digital Input Sync") != std::string_view::npos ||
        text.find("internal rca") != std::string_view::npos ||
        text.find("internal opt") != std::string_view::npos) {
        state.syncSource = BeBoBSyncSource::kInternalDigitalInput;
    } else if (text.find("Internal Sync") != std::string_view::npos ||
               text.find("internal") != std::string_view::npos) {
        state.syncSource = BeBoBSyncSource::kInternal;
    } else if (text.find("Adat External Sync") != std::string_view::npos ||
               text.find("adat") != std::string_view::npos) {
        state.syncSource = BeBoBSyncSource::kAdatExternal;
    } else if (text.find("Spdif External Sync") != std::string_view::npos ||
               text.find("spdif") != std::string_view::npos) {
        state.syncSource = BeBoBSyncSource::kSpdifExternal;
    } else if (text.find("Word Clock Sync") != std::string_view::npos ||
               text.find("word") != std::string_view::npos) {
        state.syncSource = BeBoBSyncSource::kWordClock;
    }

    // Sampling Frequency
    if (text.find("48kHz") != std::string_view::npos || text.find("48000") != std::string_view::npos) {
        state.sampleRateHz = 48000;
    } else if (text.find("44.1kHz") != std::string_view::npos || text.find("44100") != std::string_view::npos) {
        state.sampleRateHz = 44100;
    } else if (text.find("96kHz") != std::string_view::npos || text.find("96000") != std::string_view::npos) {
        state.sampleRateHz = 96000;
    } else if (text.find("88.2kHz") != std::string_view::npos || text.find("88200") != std::string_view::npos) {
        state.sampleRateHz = 88200;
    } else if (text.find("192kHz") != std::string_view::npos || text.find("192000") != std::string_view::npos) {
        state.sampleRateHz = 192000;
    } else if (text.find("176.4kHz") != std::string_view::npos || text.find("176400") != std::string_view::npos) {
        state.sampleRateHz = 176400;
    } else if (text.find("32kHz") != std::string_view::npos || text.find("32000") != std::string_view::npos) {
        state.sampleRateHz = 32000;
    }

    if (auto v = ExtractUInt64(text, "LineIn")) { state.lineInChannels = static_cast<uint8_t>(*v); }
    if (auto v = ExtractUInt64(text, "SpdifAdatIn")) { state.spdifAdatInChannels = static_cast<uint8_t>(*v); }
    if (auto v = ExtractUInt64(text, "SpdifAdatOut")) { state.spdifAdatOutChannels = static_cast<uint8_t>(*v); }
    if (auto v = ExtractUInt64(text, "MixerOut")) { state.mixerOutChannels = static_cast<uint8_t>(*v); }

    return state;
}

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
