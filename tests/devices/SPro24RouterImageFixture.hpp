// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// SPro24RouterImageFixture.hpp -- a real Saffire Pro 24 DSP router image.
//
// Captured from hardware on 2026-08-27 by reading the active low-rate router
// out of CURRENT_CONFIG at 0xFFFFE0200D24 (quadlet 0 is the entry count, then
// one quadlet per entry). It is committed for two reasons:
//
//  1. Router-image code has to be tested against a real device layout, not a
//     synthetic one. Two properties here are easy to miss when inventing a
//     fixture, and both are load-bearing:
//
//     - Entry POSITION is semantically meaningful. The peak section is a
//       parallel array of router entries indexed by position, so moving an
//       entry moves what a meter reads. Entries 0-3 are the physical-input
//       meter sources (Ins0:2, Ins0:3, Ins0:0, Ins0:1), matching the ALSA
//       tcd22xx FIXED specification.
//     - Entries 46/47 both target blk15:0 -- the same destination twice, which
//       is illegal for a real route. They exist to give Mixer:0/1 a metering
//       slot, so a validator must not reject duplicate destinations outright
//       and an editor must not compact them away.
//
//  2. It is the recovery baseline. Writing a bad router image can silence the
//     device; async register access does not pass through the audio router, so
//     rewriting these entries and re-issuing LoadRouter always remains possible.
//
// Decoding matches DecodeDiceRouterEntry: dst = value & 0xFF, src = (value >> 8)
// & 0xFF, each splitting into blk = byte >> 4 and ch = byte & 0x0F, with the
// peak value in the high 16 bits (zero throughout this capture).

#pragma once

#include <array>
#include <cstdint>

namespace ASFW::Audio::DICE::Focusrite::Testing {

/// The device reports maximum_entry_count = 128; this image uses 48.
inline constexpr uint32_t kSPro24CapturedRouterEntryCount = 48;

inline constexpr std::array<uint32_t, kSPro24CapturedRouterEntryCount>
    kSPro24CapturedRouterImage = {
    0x00004248U,  // [ 0] Ins0:8  <- Ins0:2
    0x00004349U,  // [ 1] Ins0:9  <- Ins0:3
    0x000040B2U,  // [ 2] Avs0:2  <- Ins0:0
    0x000041B3U,  // [ 3] Avs0:3  <- Ins0:1
    0x000006B4U,  // [ 4] Avs0:4  <- Aes:6
    0x000007B5U,  // [ 5] Avs0:5  <- Aes:7
    0x000010B6U,  // [ 6] Avs0:6  <- Adat:0
    0x000011B7U,  // [ 7] Avs0:7  <- Adat:1
    0x000012B8U,  // [ 8] Avs0:8  <- Adat:2
    0x000013B9U,  // [ 9] Avs0:9  <- Adat:3
    0x000014BAU,  // [10] Avs0:10 <- Adat:4
    0x000015BBU,  // [11] Avs0:11 <- Adat:5
    0x000016BCU,  // [12] Avs0:12 <- Adat:6
    0x000017BDU,  // [13] Avs0:13 <- Adat:7
    0x00002040U,  // [14] Ins0:0  <- Mixer:0
    0x00002141U,  // [15] Ins0:1  <- Mixer:1
    0x00002042U,  // [16] Ins0:2  <- Mixer:0
    0x00002143U,  // [17] Ins0:3  <- Mixer:1
    0x00002044U,  // [18] Ins0:4  <- Mixer:0
    0x00002145U,  // [19] Ins0:5  <- Mixer:1
    0x00002006U,  // [20] Aes:6  <- Mixer:0
    0x00002107U,  // [21] Aes:7  <- Mixer:1
    0x000042BEU,  // [22] Avs0:14 <- Ins0:2
    0x000043BFU,  // [23] Avs0:15 <- Ins0:3
    0x00004220U,  // [24] MixerTx0:0  <- Ins0:2
    0x00004321U,  // [25] MixerTx0:1  <- Ins0:3
    0x00004022U,  // [26] MixerTx0:2  <- Ins0:0
    0x00004123U,  // [27] MixerTx0:3  <- Ins0:1
    0x00001024U,  // [28] MixerTx0:4  <- Adat:0
    0x00001125U,  // [29] MixerTx0:5  <- Adat:1
    0x00001226U,  // [30] MixerTx0:6  <- Adat:2
    0x00001327U,  // [31] MixerTx0:7  <- Adat:3
    0x00001428U,  // [32] MixerTx0:8  <- Adat:4
    0x00001529U,  // [33] MixerTx0:9  <- Adat:5
    0x0000162AU,  // [34] MixerTx0:10 <- Adat:6
    0x0000172BU,  // [35] MixerTx0:11 <- Adat:7
    0x0000062CU,  // [36] MixerTx0:12 <- Aes:6
    0x0000072DU,  // [37] MixerTx0:13 <- Aes:7
    0x0000B02EU,  // [38] MixerTx0:14 <- Avs0:0
    0x0000B12FU,  // [39] MixerTx0:15 <- Avs0:1
    0x00004E30U,  // [40] MixerTx1:0  <- Ins0:14
    0x00004F31U,  // [41] MixerTx1:1  <- Ins0:15
    0x000048B0U,  // [42] Avs0:0  <- Ins0:8
    0x000049B1U,  // [43] Avs0:1  <- Ins0:9
    0x0000284EU,  // [44] Ins0:14 <- Mixer:8
    0x0000294FU,  // [45] Ins0:15 <- Mixer:9
    0x000020F0U,  // [46] blk15:0  <- Mixer:0
    0x000021F0U,  // [47] blk15:0  <- Mixer:1
};

/// Router entries reserved for physical-input metering. Preserve in place.
inline constexpr uint32_t kSPro24ReservedMeterEntryCount = 4;

} // namespace ASFW::Audio::DICE::Focusrite::Testing
