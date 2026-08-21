// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialMeter.hpp — pure 84-byte HSCI meter-block decoder.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ASFW::Audio::BeBoB {

struct MAudioSpecialMeterState final {
    static constexpr size_t kBlockBytes = 84;
    static constexpr size_t kPeakCount = 38; // 19 stereo points.

    std::array<int16_t, kPeakCount> peaks{};
    uint32_t detectedSampleRateHz{0};
    bool clockLocked{false};
};

[[nodiscard]] constexpr uint32_t MAudioRateFromFdf(uint8_t fdf) noexcept {
    switch (fdf & 0x07U) {
    case 0: return 32'000;
    case 1: return 44'100;
    case 2: return 48'000;
    case 3: return 88'200;
    case 4: return 96'000;
    case 5: return 176'400;
    case 6: return 192'000;
    default: return 0;
    }
}

// The physical layout is cross-validated with ALSA special.rs: 38 big-endian
// i16 samples begin at byte 4. Linux's `check_clk_sync()` is the behavioural
// reference for lock: byte 82 is locked when it is not 0xff, and then contains
// the running FDF/SFC. The vendor kext also examines adjacent status bytes, but
// that is not its public lock contract. Payload remains wire-order here.
[[nodiscard]] constexpr bool DecodeMAudioSpecialMeter(
    std::span<const uint8_t> payload, uint32_t expectedRateHz,
    MAudioSpecialMeterState& out) noexcept {
    if (payload.size() != MAudioSpecialMeterState::kBlockBytes) return false;

    MAudioSpecialMeterState decoded{};
    for (size_t i = 0; i < decoded.peaks.size(); ++i) {
        const size_t offset = 4U + i * 2U;
        decoded.peaks[i] = static_cast<int16_t>(
            (static_cast<uint16_t>(payload[offset]) << 8U) |
            static_cast<uint16_t>(payload[offset + 1U]));
    }
    const bool locked = payload[82] != 0xffU;
    decoded.detectedSampleRateHz = locked ? MAudioRateFromFdf(payload[82]) : 0U;
    decoded.clockLocked = locked && decoded.detectedSampleRateHz != 0U;
    // The meter status is a device observation. Do not silently turn a real
    // lock into "unlocked" merely because host belief has not caught up.
    (void)expectedRateHz;
    out = decoded;
    return true;
}

} // namespace ASFW::Audio::BeBoB
