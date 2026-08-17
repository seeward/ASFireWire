// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBStreamTelemetryParser.hpp — Parser for BeBoB diagnostic stdout streams.

#pragma once

#include "BeBoBTelemetryTypes.hpp"

#include <optional>
#include <string_view>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

class BeBoBStreamTelemetryParser final {
public:
    /// Parses the Output/Input Stream statistics table from `sys stat` (sub_200E1E64).
    [[nodiscard]] static std::optional<BeBoBStreamingStats> ParseStreamingStats(
        std::string_view text) noexcept;

    /// Parses the silicon hardware error status dump from `sys avstat` (sub_201222F8).
    [[nodiscard]] static std::optional<BeBoBAvStat> ParseAvStat(
        std::string_view text) noexcept;

    /// Parses the audio state and clock source status from `fw sync show` (sub_2011EC00).
    [[nodiscard]] static std::optional<BeBoBSyncState> ParseSyncState(
        std::string_view text) noexcept;
};

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
