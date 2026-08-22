// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// The capture channel map was transcribed from M-Audio's own driver, so nothing
// else in the build can catch a slipped digit. These tests pin the exact table,
// the model gate on the input skew, and the decode behaviour that depends on
// both.

#include <gtest/gtest.h>

#include "Audio/Families/BeBoB/MAudio/MAudioCaptureChannelMap.hpp"
#include "Audio/Families/BeBoB/MAudio/MAudioDuplexPolicy.hpp"
#include "Audio/Families/BeBoB/MAudio/MAudioSpecialTiming.hpp"
#include "Audio/Devices/ResolvedAudioEndpointProfile.hpp"
#include "Audio/Protocols/BeBoB/MAudioSpecialFormation.hpp"
#include "Audio/Engine/Direct/Rx/DirectRxPacketDecoder.hpp"

#include <array>
#include <vector>

namespace {

using ASFW::Audio::Families::BeBoB::MAudio::CaptureChannelMapFor;
using ASFW::AudioEngine::Direct::Rx::RxCaptureChannelMap;
using ProfileBuilderId = ASFW::DeviceProfiles::Audio::ProfileBuilderId;

// Recovered from com_m_audio_FW1814Device::WorkaroundFor1814InputDelay (0xcba4)
// and ConvertInputFrames1212CS (0x29183). Analog jacks are planar: the device
// emits the left member of all four pairs, then the right member.
constexpr std::array<uint8_t, 10> kExpectedSlots10{0, 4, 1, 5, 2, 6, 3, 7, 8, 9};

} // namespace

TEST(MAudioCaptureChannelMapTests, FireWire1814TenChannelMapMatchesTheVendorDriver) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);

    ASSERT_EQ(map.slotCount, kExpectedSlots10.size());
    for (uint32_t ch = 0; ch < kExpectedSlots10.size(); ++ch) {
        EXPECT_EQ(map.SlotFor(ch), kExpectedSlots10[ch]) << "channel " << ch;
    }

    // The observation that started this: a microphone in jack 2 reaches
    // CoreAudio input 5 without the map, because jack 2 is wire slot 4.
    EXPECT_EQ(map.SlotFor(0), 0u);
    EXPECT_EQ(map.SlotFor(1), 4u);
}

TEST(MAudioCaptureChannelMapTests, ProjectMixSharesThePermutationButHasNoInputSkew) {
    const auto fw1814 = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);
    const auto projectMix = CaptureChannelMapFor(ProfileBuilderId::MAudioProjectMix, 10);

    ASSERT_EQ(fw1814.slotCount, projectMix.slotCount);
    for (uint32_t ch = 0; ch < projectMix.slotCount; ++ch) {
        EXPECT_EQ(projectMix.SlotFor(ch), fw1814.SlotFor(ch)) << "channel " << ch;
    }

    // ProjectMix's ResetRearrangementProcs (0x1a61e) replaces the 1814's
    // converter with one that reads every channel from the current frame.
    // Applying the 1814's delay here would introduce a skew the device does not
    // have, so this gate is the whole safety property for untested hardware.
    EXPECT_FALSE(projectMix.HasDelay());
    EXPECT_EQ(projectMix.delayFrames, 0u);
    EXPECT_EQ(projectMix.delayedChannelMask, 0u);
}

TEST(MAudioCaptureChannelMapTests, FireWire1814DelaysExactlyTheSixLineInputs) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);

    // 16 banks of six channels, from the constructor's
    // `for (i = 0; i != 96; i += 6)` table fill (0xf04a).
    EXPECT_TRUE(map.HasDelay());
    EXPECT_EQ(map.delayFrames, 16u);

    // Channels 3..8 (0-based 2..7) are the line inputs. The two microphone
    // preamps and both S/PDIF channels are read from the current frame.
    for (uint32_t ch = 0; ch < 10; ++ch) {
        const bool expectDelayed = ch >= 2 && ch <= 7;
        EXPECT_EQ(map.IsDelayed(ch), expectDelayed) << "channel " << ch;
    }
}

TEST(MAudioCaptureChannelMapTests, AdatGeometryKeepsAnalogOrderAndPassesAdatThrough) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 16);

    ASSERT_EQ(map.slotCount, 16u);
    for (uint32_t ch = 0; ch < 10; ++ch) {
        EXPECT_EQ(map.SlotFor(ch), kExpectedSlots10[ch]) << "channel " << ch;
    }
    // WorkaroundFor1814InputDelay widens straight from src[10..15].
    for (uint32_t ch = 10; ch < 16; ++ch) {
        EXPECT_EQ(map.SlotFor(ch), ch) << "channel " << ch;
    }
}

TEST(MAudioCaptureChannelMapTests, NonSpecialProfilesAndUnknownGeometriesStayIdentity) {
    EXPECT_TRUE(CaptureChannelMapFor(ProfileBuilderId::TerraTecPhase88, 10).IsIdentity());
    // A formation we have no table for must decode in wire order rather than
    // apply a permutation sized for a different one.
    EXPECT_TRUE(CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 12).IsIdentity());
    EXPECT_TRUE(CaptureChannelMapFor(ProfileBuilderId::MAudioProjectMix, 4).IsIdentity());
}

TEST(MAudioCaptureChannelMapTests, MapIsRejectedWhenItCannotFitThePacket) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);
    // Real geometry: 10 PCM in an 11-slot data block (the trailing slot is MIDI).
    EXPECT_TRUE(map.FitsWithin(10, 11));
    // A device sending a narrower data block than the table addresses must not
    // be read out of bounds.
    EXPECT_FALSE(map.FitsWithin(10, 8));
    EXPECT_FALSE(map.FitsWithin(12, 11));
}

// The 1814 changes capture width when its digital format switches (10 channels
// on S/PDIF, 16 on ADAT). A table built for one width must never be applied to
// the other: silently permuting to the wrong formation would be indistinguishable
// from a correct decode, whereas falling back to wire order is both audible and
// logged.
TEST(MAudioCaptureChannelMapTests, MapBuiltForOneFormationIsRejectedByTheOther) {
    const auto spdif = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);
    const auto adat = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 16);

    EXPECT_EQ(spdif.channelCount, 10u);
    EXPECT_EQ(adat.channelCount, 16u);

    EXPECT_TRUE(spdif.FitsWithin(10, 11));
    EXPECT_FALSE(spdif.FitsWithin(16, 17));

    EXPECT_TRUE(adat.FitsWithin(16, 17));
    EXPECT_FALSE(adat.FitsWithin(10, 11));
}

TEST(MAudioCaptureChannelMapTests, ConfigurationScopedMapFollowsTheLiveFormation) {
    ASFW::Audio::Devices::ResolvedAudioEndpointProfile profile{};
    profile.configurationCapabilityCount = 2;

    auto spdifCaps = ASFW::Audio::AudioStreamRuntimeCaps{};
    spdifCaps.sampleRateHz = 48000;
    spdifCaps.hostInputPcmChannels = 10;
    spdifCaps.hostOutputPcmChannels = 10;
    spdifCaps.deviceToHostAm824Slots = 11;
    spdifCaps.hostToDeviceAm824Slots = 11;
    profile.configurationCapabilities[0].runtimeCaps = spdifCaps;
    profile.configurationCapabilities[0].captureChannelMap =
        CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);

    auto adatCaps = spdifCaps;
    adatCaps.hostInputPcmChannels = 16;
    adatCaps.hostOutputPcmChannels = 16;
    adatCaps.deviceToHostAm824Slots = 17;
    adatCaps.hostToDeviceAm824Slots = 17;
    profile.configurationCapabilities[1].runtimeCaps = adatCaps;
    profile.configurationCapabilities[1].captureChannelMap =
        CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 16);

    const auto spdifMap = profile.CaptureChannelMapForRuntimeCaps(spdifCaps);
    const auto adatMap = profile.CaptureChannelMapForRuntimeCaps(adatCaps);
    EXPECT_EQ(spdifMap.channelCount, 10u);
    EXPECT_EQ(adatMap.channelCount, 16u);
    EXPECT_TRUE(spdifMap.FitsWithin(10, 11));
    EXPECT_TRUE(adatMap.FitsWithin(16, 17));
}

// The decoder half: a mapped frame must land each slot on its channel, and send
// exactly the delayed channels to the later frame.
TEST(MAudioCaptureChannelMapTests, MappedDecodeSplitsDelayedChannelsIntoTheLaterFrame) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814, 10);

    // One AM824 data block: slot N carries value (N+1) << 8 so each slot is
    // distinguishable and safely inside 24-bit range.
    std::array<uint32_t, 11> wire{};
    for (uint32_t slot = 0; slot < 11; ++slot) {
        const uint32_t sample = (slot + 1u) << 8;
        wire[slot] = OSSwapHostToBigInt32(0x40000000u | sample);
    }

    std::array<float, 10> now{};
    std::array<float, 10> later{};
    now.fill(-1.0f);
    later.fill(-1.0f);

    ASFW::AudioEngine::Direct::Rx::DecodeDirectRxFrameMapped(
        wire.data(), 10, ASFW::Encoding::AudioWireFormat::kAM824, map,
        now.data(), later.data());

    const auto expectedFor = [](uint32_t slot) {
        return static_cast<float>((slot + 1u) << 8) / 8388607.0f;
    };

    for (uint32_t ch = 0; ch < 10; ++ch) {
        const uint32_t slot = kExpectedSlots10[ch];
        if (ch >= 2 && ch <= 7) {
            EXPECT_FLOAT_EQ(later[ch], expectedFor(slot)) << "delayed channel " << ch;
            EXPECT_FLOAT_EQ(now[ch], -1.0f) << "channel " << ch << " must not write now";
        } else {
            EXPECT_FLOAT_EQ(now[ch], expectedFor(slot)) << "channel " << ch;
            EXPECT_FLOAT_EQ(later[ch], -1.0f) << "channel " << ch << " must not write later";
        }
    }
}

TEST(MAudioCaptureChannelMapTests, ProjectMixMappedDecodeWritesEveryChannelToTheCurrentFrame) {
    const auto map = CaptureChannelMapFor(ProfileBuilderId::MAudioProjectMix, 10);

    std::array<uint32_t, 11> wire{};
    for (uint32_t slot = 0; slot < 11; ++slot) {
        wire[slot] = OSSwapHostToBigInt32(0x40000000u | ((slot + 1u) << 8));
    }

    std::array<float, 10> now{};
    std::array<float, 10> later{};
    now.fill(-1.0f);
    later.fill(-1.0f);

    ASFW::AudioEngine::Direct::Rx::DecodeDirectRxFrameMapped(
        wire.data(), 10, ASFW::Encoding::AudioWireFormat::kAM824, map,
        now.data(), later.data());

    for (uint32_t ch = 0; ch < 10; ++ch) {
        const float expected =
            static_cast<float>((kExpectedSlots10[ch] + 1u) << 8) / 8388607.0f;
        EXPECT_FLOAT_EQ(now[ch], expected) << "channel " << ch;
        EXPECT_FLOAT_EQ(later[ch], -1.0f) << "channel " << ch << " must not be delayed";
    }
}

// ProjectMix I/O is untestable here — no unit available — so the properties that
// keep it working are pinned as assertions instead. Both reference stacks say
// the two personas share everything except the clock and the capture converter:
// the kext's `com_m_audio_FWProjectMixDevice` derives from
// `com_m_audio_FW1814Device` and overrides only geometry, that converter,
// digital-signal switching, factory reset and engine setup, while ALSA defines
// both as `SpecialModel<T>` parameterised solely by the clock protocol.
TEST(MAudioCaptureChannelMapTests, BothPersonasAreDrivenByTheSameSpecialPolicy) {
    using ASFW::Audio::Families::BeBoB::MAudio::UsesSpecialDuplexPolicy;

    // The duplex choreography, the cadence-packet policy and the capability
    // envelope all key off this predicate, so a persona missing from it silently
    // loses the whole special-firmware stack.
    EXPECT_TRUE(UsesSpecialDuplexPolicy(ProfileBuilderId::MAudioFireWire1814));
    EXPECT_TRUE(UsesSpecialDuplexPolicy(ProfileBuilderId::MAudioProjectMix));
    EXPECT_FALSE(UsesSpecialDuplexPolicy(ProfileBuilderId::TerraTecPhase88));

    // Both get a capture map at both real formations...
    for (const uint32_t channels : {10U, 16U}) {
        EXPECT_FALSE(CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814,
                                          channels).IsIdentity())
            << "1814 at " << channels;
        EXPECT_FALSE(CaptureChannelMapFor(ProfileBuilderId::MAudioProjectMix,
                                          channels).IsIdentity())
            << "ProjectMix at " << channels;
    }

    // ...and the skew stays 1814-only at every geometry. This is the assertion
    // that protects hardware we cannot test: applying the delay to a ProjectMix
    // would introduce a 333 us error on six channels.
    for (const uint32_t channels : {10U, 16U}) {
        EXPECT_TRUE(CaptureChannelMapFor(ProfileBuilderId::MAudioFireWire1814,
                                         channels).HasDelay());
        EXPECT_FALSE(CaptureChannelMapFor(ProfileBuilderId::MAudioProjectMix,
                                          channels).HasDelay());
    }
}

// The vendor keeps reported latency and safety offset as two separate device
// methods, and `m_audio_b_FWBaseEngine::ResetLatency` splits the round-trip
// figure in half across the two directions. Pinning both, because these are
// transcribed tables and a slipped digit lands recordings at the wrong offset
// (latency) or produces dropouts (safety offset).
TEST(MAudioCaptureChannelMapTests, VendorLatencyTablesAreReproducedExactly) {
    using ASFW::Audio::Families::BeBoB::MAudio::SpecialRateTiming;
    using ASFW::Audio::Families::BeBoB::MAudio::SpecialRateTimingFor;

    struct Expectation {
        uint32_t rateHz;
        uint32_t roundTrip;
        uint32_t safety;
    };
    // com_m_audio_FW1814Device::GetRoundTripLatencyForFDF @ 0xcf30
    // m_audio_b_FWAudioDevice::GetSafetyOffsetForFDF      @ 0x1d58
    constexpr Expectation k1814[]{
        {44100, 210, 44}, {48000, 225, 48}, {88200, 358, 88},
        {96000, 378, 96}, {176400, 642, 176}, {192000, 698, 192},
    };
    // com_m_audio_FWProjectMixDevice::GetRoundTripLatencyForFDF @ 0x1a5b0
    constexpr Expectation kProjectMix[]{
        {44100, 196, 44}, {48000, 208, 48}, {88200, 342, 88}, {96000, 366, 96},
    };

    const auto check = [](ProfileBuilderId id, const Expectation& e) {
        SpecialRateTiming timing{};
        ASSERT_TRUE(SpecialRateTimingFor(id, e.rateHz, timing)) << e.rateHz;
        // Input takes the rounded-up half, output the rounded-down half, and the
        // two must sum back to the vendor's round-trip figure.
        EXPECT_EQ(timing.inputLatencyFrames, (e.roundTrip + 1U) / 2U) << e.rateHz;
        EXPECT_EQ(timing.outputLatencyFrames, e.roundTrip / 2U) << e.rateHz;
        EXPECT_EQ(timing.inputLatencyFrames + timing.outputLatencyFrames,
                  e.roundTrip) << e.rateHz;
        EXPECT_EQ(timing.safetyOffsetFrames, e.safety) << e.rateHz;
    };
    for (const auto& e : k1814) { check(ProfileBuilderId::MAudioFireWire1814, e); }
    for (const auto& e : kProjectMix) { check(ProfileBuilderId::MAudioProjectMix, e); }

    // Every safety offset is one millisecond of frames at its rate, which is
    // almost certainly how the vendor chose the table.
    for (const auto& e : k1814) {
        EXPECT_EQ(e.safety, e.rateHz / 1000U) << e.rateHz;
    }

    // Nothing else gets these tables.
    SpecialRateTiming ignored{};
    EXPECT_FALSE(SpecialRateTimingFor(ProfileBuilderId::TerraTecPhase88, 48000, ignored));
}

// The port count is NOT a slot count. Both personas carry one AM824
// conformant-data slot, so DBS is identical; only the number of ports muxed
// through it differs. Getting this wrong would inflate DBS to 12 and make every
// packet a geometry mismatch.
TEST(MAudioCaptureChannelMapTests, MidiPortCountDiffersButTheSlotCountDoesNot) {
    using ASFW::Audio::Families::BeBoB::MAudio::SpecialMidiPortCount;

    EXPECT_EQ(SpecialMidiPortCount(ProfileBuilderId::MAudioFireWire1814), 1U);
    EXPECT_EQ(SpecialMidiPortCount(ProfileBuilderId::MAudioProjectMix), 2U);
    EXPECT_EQ(SpecialMidiPortCount(ProfileBuilderId::TerraTecPhase88), 0U);

    // The formation is what decides DBS, and it is one conformant-data block for
    // both regardless of port count (Linux bebob_maudio.c:249,252).
    for (const uint32_t rateHz : {44100U, 48000U}) {
        const auto formation = ::ASFW::Audio::BeBoB::MAudioFormationFor(
            ::ASFW::Audio::BeBoB::MAudioDigitalFormat::SPDIF,
            ::ASFW::Audio::BeBoB::MAudioDigitalFormat::SPDIF, rateHz);
        ASSERT_TRUE(formation.has_value()) << rateHz;
        EXPECT_EQ(formation->midiDataBlocks, 1U) << rateHz;
        // 10 PCM + 1 MIDI = DBS 11, the value measured on the wire.
        EXPECT_EQ(formation->capturePcmChannels + formation->midiDataBlocks, 11U)
            << rateHz;
    }
}
