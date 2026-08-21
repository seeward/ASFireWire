// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialMeter.hpp — 84-byte HSCI meter-block decoder for the FireWire
// 1814, read from 0xffc700600000.
//
// The block carries three different things at once: 38 peak levels, four
// front-panel event bytes, and a two-byte clock status tail. The event bytes are
// *edge-triggered* — the device reports "the knob moved one detent" rather than
// a position — so decoding them requires the previous block, which is why this
// decoder is stateful. State lives in MAudioSpecialMeterState and is advanced in
// place.
//
// Layout evidence:
//   byte 0  & 3   momentary switch event
//   byte 1  & 3   headphone knob 1
//   byte 2  & 3   headphone knob 2
//   byte 3  & 3   assignable knob
//   bytes 4..79   38 big-endian i16 peaks
//   bytes 80..81  unclaimed by any reference
//   byte 82       FDF / SFC of the detected clock
//   byte 83       bit 0 = external sync
//
// The first quadlet's assignment comes from the vendor kext:
// `com_m_audio_FW1814Device::ReceiveControlPacket` @ 0xe9ea reads byte 3 into
// `UserRotatedHardwareKnob`, byte 2 into headphone pair 1, byte 1 into headphone
// pair 0, and byte 0 into `MomentarySwitchPressed`. The event decode is
// `TRotaryControl::RotaryValueChanged` @ 0xc2ba: field 1 = one detent up,
// field 2 = one detent down, anything else = no event.
//
// **The knobs do nothing on their own.** They are relative encoders; the vendor
// driver converts a detent into a new headphone level and writes it back into
// the parameter window. A host that only reads them leaves the knob inert.
//
// The peaks and the event bytes come from the ALSA crate
// (protocols/bebob/src/maudio/special.rs, MaudioSpecialMeterProtocol::cache).
// The two status bytes come from the vendor kext, which is the only reference
// that reads them as fields rather than as change detectors:
// `com_m_audio_FW1814Device::ReadHSCIData` @ 0xd952 calls
// `DetectExternalSyncChanges(byte83 & 1, byte82 & 7)`. See
// docs/MAUDIO_1814_KEXT_RE.md §6.
//
// Linux's `check_clk_sync()` tests `buf[82] != 0xff`, which is the same test
// seen from the FDF side and agrees. The crate's `sync_status` is a change
// detector on byte 83 rather than a read of its bit 0 and is not followed.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ASFW::Audio::BeBoB {

/// Which front-panel control each rotary index corresponds to. Index order is
/// meter-block byte order (bytes 1, 2, 3).
enum class MAudio1814Rotary : uint8_t {
    Headphone12 = 0, ///< byte 1 — headphone pair 1 volume
    Headphone34 = 1, ///< byte 2 — headphone pair 2 volume
    Assignable = 2,  ///< byte 3 — user-assigned across five level groups
};

/// One decoded detent event, in units of `kRotaryStep`.
struct MAudio1814RotaryDelta final {
    int32_t detents[3]{};
    [[nodiscard]] constexpr bool Any() const noexcept {
        return detents[0] != 0 || detents[1] != 0 || detents[2] != 0;
    }
};

/// Order of the 38 peak points in the block. The device does not label them; this
/// is the crate's chained decode order, which is also the order the vendor's
/// `Sample1814Peaks*` samplers fill.
enum class MAudio1814MeterSection : uint8_t {
    AnalogInput,   ///< 8
    SpdifInput,    ///< 2
    AdatInput,     ///< 8
    AnalogOutput,  ///< 4
    SpdifOutput,   ///< 2
    AdatOutput,    ///< 8
    Headphone,     ///< 4
    AuxOutput,     ///< 2
};

struct MAudio1814MeterSectionInfo final {
    MAudio1814MeterSection section{};
    uint8_t firstIndex{};
    uint8_t count{};
    const char* name{nullptr};
};

inline constexpr std::array<MAudio1814MeterSectionInfo, 8> kMAudio1814MeterSections{{
    {MAudio1814MeterSection::AnalogInput, 0, 8, "analog in"},
    {MAudio1814MeterSection::SpdifInput, 8, 2, "S/PDIF in"},
    {MAudio1814MeterSection::AdatInput, 10, 8, "ADAT in"},
    {MAudio1814MeterSection::AnalogOutput, 18, 4, "analog out"},
    {MAudio1814MeterSection::SpdifOutput, 22, 2, "S/PDIF out"},
    {MAudio1814MeterSection::AdatOutput, 24, 8, "ADAT out"},
    {MAudio1814MeterSection::Headphone, 32, 4, "headphone"},
    {MAudio1814MeterSection::AuxOutput, 36, 2, "aux out"},
}};

struct MAudioSpecialMeterState final {
    /// What the decoder needs. Linux (`METER_SIZE_SPECIAL`) and the ALSA crate
    /// (`METER_SIZE`) both read exactly this much.
    static constexpr size_t kBlockBytes = 84;
    /// What the vendor actually reads:
    /// `com_m_audio_FW1814Device::GetControlPacketSize` @ 0xc962 returns **88**.
    /// Bytes 84..87 are read by the vendor and used by nothing we have found, and
    /// no reference implementation reads them at all. We request the vendor's
    /// size so the device sees the transaction it expects, and so the bytes are
    /// available to look at.
    static constexpr size_t kVendorBlockBytes = 88;
    static constexpr size_t kPeakCount = 38;
    static constexpr size_t kRotaryCount = 3;

    /// Accumulated rotary bounds. The knobs are relative encoders, so a position
    /// exists only because this decoder integrates the detent events; it starts
    /// at the top and is not a readback of anything.
    static constexpr int16_t kRotaryMin = -32768;
    static constexpr int16_t kRotaryMax = 0;
    static constexpr int16_t kRotaryStep = 0x400;

    std::array<int16_t, kPeakCount> peaks{};
    uint32_t detectedSampleRateHz{0};
    bool clockLocked{false};
    bool externalSync{false};
    bool hardwareSwitch{false};
    std::array<int16_t, kRotaryCount> rotaries{};

    /// Previous block's event bytes, kept so the next decode can find the edges.
    /// `hasPreviousEvents` suppresses the first block, where every byte looks
    /// like a change and would fake a knob turn on every connect.
    std::array<uint8_t, 4> previousEvents{};
    bool hasPreviousEvents{false};
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

/// Advances `inOut` with one freshly read meter block. Returns false and leaves
/// the state untouched when the block is the wrong size.
///
/// Peaks and clock status are absolute and simply replace the previous values.
/// The switch and rotaries are integrated from edge events: a byte that changed
/// since the previous block and now reads 0x01 or 0x02 is one detent. Both
/// reference implementations agree on that encoding; only the crate implements
/// it, and this follows the crate's step and clamping.
[[nodiscard]] constexpr bool DecodeMAudioSpecialMeter(
    std::span<const uint8_t> payload, MAudioSpecialMeterState& inOut,
    MAudio1814RotaryDelta* outDeltas = nullptr) noexcept {
    // Accept either size: the decoder only ever reads the first 84 bytes.
    if (payload.size() != MAudioSpecialMeterState::kBlockBytes &&
        payload.size() != MAudioSpecialMeterState::kVendorBlockBytes) {
        return false;
    }
    if (outDeltas) *outDeltas = {};

    for (size_t i = 0; i < inOut.peaks.size(); ++i) {
        const size_t offset = 4U + i * 2U;
        inOut.peaks[i] = static_cast<int16_t>(
            (static_cast<uint16_t>(payload[offset]) << 8U) |
            static_cast<uint16_t>(payload[offset + 1U]));
    }

    // Byte 82 holds the running FDF when the device has a clock and 0xff when it
    // does not, so a decodable rate code *is* the lock indication.
    const uint8_t fdf = payload[82];
    const uint32_t detected = (fdf != 0xFFU) ? MAudioRateFromFdf(fdf) : 0U;
    inOut.detectedSampleRateHz = detected;
    inOut.clockLocked = detected != 0U;
    inOut.externalSync = (payload[83] & 0x01U) != 0U;

    // The vendor masks each event field to two bits; do the same rather than
    // comparing whole bytes, so an unmodelled high bit cannot swallow a detent.
    const auto event = [](uint8_t raw) constexpr { return static_cast<uint8_t>(raw & 0x03U); };

    if (inOut.hasPreviousEvents) {
        if (event(payload[0]) != event(inOut.previousEvents[0]) && event(payload[0]) == 0x01U) {
            inOut.hardwareSwitch = !inOut.hardwareSwitch;
        }
        for (size_t i = 0; i < inOut.rotaries.size(); ++i) {
            const size_t pos = i + 1U;
            const uint8_t now = event(payload[pos]);
            // Edge-gated, following the crate. The kext acts on the field's
            // value every poll instead, with no comparison against the previous
            // block; if the device holds 1 for the duration of a turn that would
            // scale detents with the poll rate. Under-counting a fast turn is the
            // safer failure, so the edge wins until hardware settles it.
            if (now == event(inOut.previousEvents[pos])) continue;
            int32_t detents = 0;
            if (now == 0x01U) {
                detents = 1;
            } else if (now == 0x02U) {
                detents = -1;
            } else {
                continue;
            }
            if (outDeltas) outDeltas->detents[i] = detents;

            int32_t position =
                inOut.rotaries[i] + detents * MAudioSpecialMeterState::kRotaryStep;
            if (position > MAudioSpecialMeterState::kRotaryMax) {
                position = MAudioSpecialMeterState::kRotaryMax;
            } else if (position < MAudioSpecialMeterState::kRotaryMin) {
                position = MAudioSpecialMeterState::kRotaryMin;
            }
            inOut.rotaries[i] = static_cast<int16_t>(position);
        }
    }

    for (size_t i = 0; i < inOut.previousEvents.size(); ++i) {
        inOut.previousEvents[i] = payload[i];
    }
    inOut.hasPreviousEvents = true;
    return true;
}

} // namespace ASFW::Audio::BeBoB
