// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// The second DICE stream-geometry handshake: the TCAT protocol extension's
// CURRENT_CONFIG section.
//
// A DICE device may describe its stream geometry two ways. The plain TX/RX
// stream-format sections describe only the rate mode the device happens to be
// running; the extension's CURRENT_CONFIG section stores one block per rate
// mode and can be read without moving the clock. Linux tries the extension
// first and falls back to the plain sections when the device does not implement
// it (sound/firewire/dice/dice-stream.c:609-621).
//
// The Focusrite Liquid Saffire 56 exists in two field revisions we cannot tell
// apart from identity, so which handshake answers has to be decided at runtime.
// Nobody on the project owns one: these tests are the only place the branch is
// exercised, so they cover both answers and every give-up path.

#include <gtest/gtest.h>

#include "Testing/HostDriverKitStubs.hpp"
#include "Async/Interfaces/IFireWireBus.hpp"
#include "Common/WireFormat.hpp"
#include "Discovery/DeviceRegistry.hpp"
#include "Audio/Protocols/DICE/Core/DICETypes.hpp"
#include "Audio/Protocols/DICE/TCAT/DICETcatProtocol.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <vector>

namespace {

using ASFW::Async::AsyncHandle;
using ASFW::Async::AsyncStatus;
using ASFW::Async::FWAddress;
using ASFW::Async::IFireWireBus;
using ASFW::Audio::AudioStreamRuntimeCaps;
using ASFW::Audio::DICE::DiceRateMode;
using ASFW::Audio::DICE::ExtensionSections;
using ASFW::Audio::DICE::GeneralSections;
using ASFW::Audio::DICE::HasDistinctExtensionSectionOffsets;
using ASFW::Audio::DICE::MergeExtensionStreamGeometry;
using ASFW::Audio::DICE::StreamConfig;
using ASFW::Audio::DICE::StreamFormatEntry;
using ASFW::Audio::DICE::TCAT::DICETcatProtocol;
using ASFW::Audio::DICE::TCAT::DICETcatRuntimePolicy;
using ASFW::FW::FwSpeed;
using ASFW::FW::Generation;
using ASFW::FW::LockOp;
using ASFW::FW::NodeId;

constexpr uint32_t kDiceBaseLo = static_cast<uint32_t>(
    ASFW::Audio::DICE::DICEAbsoluteAddress(0) & 0xFFFFFFFFULL);
constexpr uint32_t kExtensionBaseLo = static_cast<uint32_t>(
    ASFW::Audio::DICE::DICEAbsoluteAddress(ASFW::Audio::DICE::kDICEExtensionOffset) &
    0xFFFFFFFFULL);

// General-section layout, in quadlets, matching the shape real DICE parts
// publish. Byte offsets are 4x these.
constexpr uint32_t kGlobalOffsetQuads = 0x0A;
constexpr uint32_t kGlobalSizeQuads = 0x5F;
constexpr uint32_t kTxOffsetQuads = 0x69;
constexpr uint32_t kRxOffsetQuads = 0xF7;
// 8-byte header + one 272-byte entry.
constexpr uint32_t kStreamSectionSizeQuads = 0x46;
constexpr uint32_t kStreamEntryQuads = 0x44;

constexpr uint32_t kCurrentConfigOffsetQuads = 0x0518;
constexpr uint32_t kCurrentConfigSizeQuads = 0x1800;  // 0x6000 bytes
constexpr uint32_t kCurrentConfigOffsetBytes = kCurrentConfigOffsetQuads * 4U;

void PutBe32(std::vector<uint8_t>& image, size_t offset, uint32_t value) {
    ASSERT_LE(offset + 4, image.size());
    ASFW::FW::WriteBE32(image.data() + offset, value);
}

void PutLabels(std::vector<uint8_t>& image, size_t offset, const char* text) {
    const size_t length = std::strlen(text);
    ASSERT_LE(offset + length, image.size());
    std::memcpy(image.data() + offset, text, length);
}

// ---------------------------------------------------------------------------
// A fake bus that serves flat byte images out of registered address ranges, so
// the chunked section reader is exercised for real (512-byte chunks) instead of
// being short-circuited by a per-address special case.
// ---------------------------------------------------------------------------
class RegionFireWireBus final : public IFireWireBus {
public:
    void AddRegion(uint32_t addressLo, std::vector<uint8_t> bytes) {
        regions_[addressLo] = std::move(bytes);
    }

    AsyncHandle ReadBlock(Generation generation,
                          NodeId nodeId,
                          FWAddress address,
                          uint32_t length,
                          FwSpeed speed,
                          ASFW::Async::InterfaceCompletionCallback callback) override {
        (void)nodeId;
        (void)speed;
        ++readCount;
        if (generation != generation_ || address.addressHi != 0xFFFFU) {
            callback(AsyncStatus::kHardwareError, {});
            return NextHandle();
        }

        for (const auto& [base, bytes] : regions_) {
            if (address.addressLo < base || address.addressLo >= base + bytes.size()) {
                continue;
            }
            const size_t start = address.addressLo - base;
            const size_t available = bytes.size() - start;
            const size_t serve = (length < available) ? length : available;
            callback(AsyncStatus::kSuccess,
                     std::span<const uint8_t>(bytes.data() + start, serve));
            return NextHandle();
        }

        ++unmappedReadCount;
        callback(AsyncStatus::kHardwareError, {});
        return NextHandle();
    }

    AsyncHandle WriteBlock(Generation, NodeId, FWAddress, std::span<const uint8_t>,
                           FwSpeed,
                           ASFW::Async::InterfaceCompletionCallback callback) override {
        ++writeCount;
        callback(AsyncStatus::kSuccess, {});
        return NextHandle();
    }

    AsyncHandle Lock(Generation, NodeId, FWAddress, LockOp, std::span<const uint8_t>,
                     uint32_t responseLength, FwSpeed,
                     ASFW::Async::InterfaceCompletionCallback callback) override {
        std::vector<uint8_t> payload(responseLength, 0);
        callback(AsyncStatus::kSuccess,
                 std::span<const uint8_t>(payload.data(), payload.size()));
        return NextHandle();
    }

    bool Cancel(AsyncHandle) override { return false; }
    FwSpeed GetSpeed(NodeId) const override { return FwSpeed::S400; }
    uint32_t HopCount(NodeId, NodeId) const override { return 1; }
    Generation GetGeneration() const override { return generation_; }
    NodeId GetLocalNodeID() const override { return NodeId{0}; }

    int readCount{0};
    int writeCount{0};
    int unmappedReadCount{0};

private:
    AsyncHandle NextHandle() { return AsyncHandle{static_cast<uint32_t>(nextHandle_++)}; }

    std::map<uint32_t, std::vector<uint8_t>> regions_;
    Generation generation_{1};
    uint64_t nextHandle_{1};
};

struct RouteState {
    ASFW::Discovery::DeviceRegistry registry;
    ASFW::Discovery::DeviceRouteToken route{};

    RouteState() {
        ASFW::Discovery::ConfigROM rom{};
        rom.bib.guid = 0x00130E0000000056ULL;
        rom.gen = Generation{1};
        rom.nodeId = 2;
        const auto record = registry.UpsertFromROM(rom, ASFW::Discovery::LinkPolicy{});
        route = *registry.CurrentRoute(record.instanceId);
    }
};

std::vector<uint8_t> MakeGeneralSectionsImage() {
    std::vector<uint8_t> image(GeneralSections::kWireSize, 0);
    PutBe32(image, 0x00, kGlobalOffsetQuads);
    PutBe32(image, 0x04, kGlobalSizeQuads);
    PutBe32(image, 0x08, kTxOffsetQuads);
    PutBe32(image, 0x0C, kStreamSectionSizeQuads);
    PutBe32(image, 0x10, kRxOffsetQuads);
    PutBe32(image, 0x14, kStreamSectionSizeQuads);
    return image;
}

std::vector<uint8_t> MakeGlobalImage() {
    std::vector<uint8_t> image(kGlobalSizeQuads * 4U, 0);
    PutBe32(image, ASFW::Audio::DICE::GlobalOffset::kClockSelect,
            (ASFW::Audio::DICE::ClockRateIndex::k48000
             << ASFW::Audio::DICE::ClockSelect::kRateShift) |
                static_cast<uint32_t>(ASFW::Audio::DICE::ClockSource::Internal));
    PutBe32(image, ASFW::Audio::DICE::GlobalOffset::kStatus,
            ASFW::Audio::DICE::StatusBits::kSourceLocked);
    PutBe32(image, ASFW::Audio::DICE::GlobalOffset::kSampleRate, 48000U);
    PutBe32(image, ASFW::Audio::DICE::GlobalOffset::kClockCaps, 0x00001E06U);
    return image;
}

// The plain sections as a device left at a 2x rate reports them: one narrow
// stream per direction. Extension-preferring devices must not publish this.
std::vector<uint8_t> MakePlainStreamImage(bool isRx, int32_t isoChannel,
                                          uint32_t pcmChannels, uint32_t midiPorts) {
    std::vector<uint8_t> image(kStreamSectionSizeQuads * 4U, 0);
    PutBe32(image, 0x00, 1U);                  // stream count
    PutBe32(image, 0x04, kStreamEntryQuads);   // entry size, in quadlets
    const size_t entry = 8;
    PutBe32(image, entry + 0x00, static_cast<uint32_t>(isoChannel));
    if (isRx) {
        PutBe32(image, entry + 0x04, 0U);           // seq start
        PutBe32(image, entry + 0x08, pcmChannels);
        PutBe32(image, entry + 0x0C, midiPorts);
    } else {
        PutBe32(image, entry + 0x04, pcmChannels);
        PutBe32(image, entry + 0x08, midiPorts);
        PutBe32(image, entry + 0x0C, 2U);           // speed
    }
    PutLabels(image, entry + 16, isRx ? "plain-out\\\\" : "plain-in\\\\");
    return image;
}

struct ExtensionEntry {
    uint32_t pcmChannels;
    uint32_t midiPorts;
    const char* labels;
};

// One CURRENT_CONFIG rate-mode block, written at the low mode's offset inside a
// full-size section image.
std::vector<uint8_t> MakeCurrentConfigImage(const std::vector<ExtensionEntry>& tx,
                                            const std::vector<ExtensionEntry>& rx) {
    namespace Stream = ASFW::Audio::DICE::CurrentConfigStream;
    std::vector<uint8_t> image(kCurrentConfigSizeQuads * 4U, 0);
    const size_t block =
        ASFW::Audio::DICE::CurrentConfigStreamBlockOffset(DiceRateMode::Low);

    PutBe32(image, block + Stream::kTxNumber, static_cast<uint32_t>(tx.size()));
    PutBe32(image, block + Stream::kRxNumber, static_cast<uint32_t>(rx.size()));

    size_t index = 0;
    const auto writeEntries = [&](const std::vector<ExtensionEntry>& entries) {
        for (const auto& entry : entries) {
            const size_t base = block + Stream::kEntries + index * Stream::kEntryStride;
            PutBe32(image, base + Stream::kEntryPcmChannels, entry.pcmChannels);
            PutBe32(image, base + Stream::kEntryMidiPorts, entry.midiPorts);
            PutLabels(image, base + Stream::kEntryNames, entry.labels);
            ++index;
        }
    };
    writeEntries(tx);
    writeEntries(rx);
    return image;
}

std::vector<uint8_t> MakeExtensionSectionsImage(uint32_t currentConfigOffsetQuads,
                                                uint32_t currentConfigSizeQuads) {
    std::vector<uint8_t> image(ExtensionSections::kWireSize, 0);
    const uint32_t quadlets[18] = {
        0x0012, 0x0004,                                        // caps
        0x0016, 0x0002,                                        // command
        0x0018, 0x0100,                                        // mixer
        0x0118, 0x0100,                                        // peak
        0x0218, 0x0200,                                        // router
        0x0418, 0x0100,                                        // stream format
        currentConfigOffsetQuads, currentConfigSizeQuads,      // current config
        0x1D18, 0x0040,                                        // standalone
        0x1D58, 0x0400,                                        // application
    };
    for (size_t i = 0; i < 18; ++i) {
        PutBe32(image, i * 4, quadlets[i]);
    }
    return image;
}

// What a device WITHOUT the protocol extension answers with: the read succeeds
// and the table repeats instead of failing, which is exactly why a successful
// read is not evidence of support.
std::vector<uint8_t> MakeDegenerateExtensionSectionsImage() {
    std::vector<uint8_t> image(ExtensionSections::kWireSize, 0);
    for (size_t i = 0; i < 18; i += 2) {
        PutBe32(image, i * 4, 0x0012);
        PutBe32(image, (i + 1) * 4, 0x0004);
    }
    return image;
}

enum class ExtensionShape { Present, Degenerate, CurrentConfigTooSmall };

void InstallDevice(RegionFireWireBus& bus, ExtensionShape shape) {
    bus.AddRegion(kDiceBaseLo, MakeGeneralSectionsImage());
    bus.AddRegion(kDiceBaseLo + kGlobalOffsetQuads * 4U, MakeGlobalImage());
    bus.AddRegion(kDiceBaseLo + kTxOffsetQuads * 4U,
                  MakePlainStreamImage(false, 5, 10, 1));
    bus.AddRegion(kDiceBaseLo + kRxOffsetQuads * 4U,
                  MakePlainStreamImage(true, 6, 10, 1));

    switch (shape) {
        case ExtensionShape::Present:
            bus.AddRegion(kExtensionBaseLo,
                          MakeExtensionSectionsImage(kCurrentConfigOffsetQuads,
                                                     kCurrentConfigSizeQuads));
            bus.AddRegion(kExtensionBaseLo + kCurrentConfigOffsetBytes,
                          MakeCurrentConfigImage({{16, 1, "ext-in-a\\\\"}, {12, 0, "ext-in-b\\\\"}},
                                                 {{16, 1, "ext-out-a\\\\"}, {12, 0, "ext-out-b\\\\"}}));
            break;
        case ExtensionShape::Degenerate:
            bus.AddRegion(kExtensionBaseLo, MakeDegenerateExtensionSectionsImage());
            break;
        case ExtensionShape::CurrentConfigTooSmall:
            // Distinct offsets, so the table passes the support screen, but the
            // section is too short to hold the low rate mode's block.
            bus.AddRegion(kExtensionBaseLo,
                          MakeExtensionSectionsImage(kCurrentConfigOffsetQuads, 0x0100));
            bus.AddRegion(kExtensionBaseLo + kCurrentConfigOffsetBytes,
                          std::vector<uint8_t>(0x0100 * 4U, 0));
            break;
    }
}

AudioStreamRuntimeCaps LoadCaps(RegionFireWireBus& bus, RouteState& routeState,
                                bool preferExtension) {
    DICETcatProtocol protocol(bus, bus, routeState.registry, routeState.route, nullptr,
                              nullptr,
                              DICETcatRuntimePolicy{
                                  .preferExtensionStreamGeometry = preferExtension,
                              });
    EXPECT_EQ(protocol.Initialize(), kIOReturnSuccess);

    IOReturn status = kIOReturnError;
    bool called = false;
    protocol.EnsureRuntimeStreamGeometry([&](IOReturn result) {
        status = result;
        called = true;
    });
    EXPECT_TRUE(called) << "the geometry chain must complete synchronously on the fake bus";
    EXPECT_EQ(status, kIOReturnSuccess);

    AudioStreamRuntimeCaps caps{};
    EXPECT_TRUE(protocol.GetRuntimeAudioStreamCaps(caps));
    return caps;
}

} // namespace

// ---------------------------------------------------------------------------
// The support screen
// ---------------------------------------------------------------------------

TEST(DiceExtensionGeometry, DistinctOffsetsScreenSeparatesRealExtensionsFromEchoes) {
    const auto present = ExtensionSections::Deserialize(
        MakeExtensionSectionsImage(kCurrentConfigOffsetQuads, kCurrentConfigSizeQuads).data());
    EXPECT_TRUE(HasDistinctExtensionSectionOffsets(present));

    const auto degenerate =
        ExtensionSections::Deserialize(MakeDegenerateExtensionSectionsImage().data());
    EXPECT_FALSE(HasDistinctExtensionSectionOffsets(degenerate));

    // A single repeated pair is enough to disqualify the table: partial repeats
    // are how a non-extension device's aliased register window reads back.
    auto oneRepeat = present;
    oneRepeat.standalone.offset = oneRepeat.router.offset;
    EXPECT_FALSE(HasDistinctExtensionSectionOffsets(oneRepeat));

    // An all-zero table (no device answering the window) is degenerate too.
    EXPECT_FALSE(HasDistinctExtensionSectionOffsets(ExtensionSections{}));
}

TEST(DiceExtensionGeometry, RateModeMappingMatchesTheDiceMultiplierGroups) {
    using ASFW::Audio::DICE::CurrentConfigStreamBlockOffset;
    using ASFW::Audio::DICE::DiceRateModeForRate;

    DiceRateMode mode{};
    for (const uint32_t rate : {32000U, 44100U, 48000U}) {
        ASSERT_TRUE(DiceRateModeForRate(rate, mode)) << rate;
        EXPECT_EQ(mode, DiceRateMode::Low) << rate;
    }
    for (const uint32_t rate : {88200U, 96000U}) {
        ASSERT_TRUE(DiceRateModeForRate(rate, mode)) << rate;
        EXPECT_EQ(mode, DiceRateMode::Middle) << rate;
    }
    for (const uint32_t rate : {176400U, 192000U}) {
        ASSERT_TRUE(DiceRateModeForRate(rate, mode)) << rate;
        EXPECT_EQ(mode, DiceRateMode::High) << rate;
    }
    EXPECT_FALSE(DiceRateModeForRate(64000U, mode));

    // Block stride: 0x2000 per rate mode, streams at +0x1000 within each.
    EXPECT_EQ(CurrentConfigStreamBlockOffset(DiceRateMode::Low), 0x1000U);
    EXPECT_EQ(CurrentConfigStreamBlockOffset(DiceRateMode::Middle), 0x3000U);
    EXPECT_EQ(CurrentConfigStreamBlockOffset(DiceRateMode::High), 0x5000U);
}

// ---------------------------------------------------------------------------
// The merge
// ---------------------------------------------------------------------------

TEST(DiceExtensionGeometry, MergeKeepsWireIdentityAndTakesChannelsFromTheExtension) {
    StreamConfig plain{};
    plain.isRxLayout = false;
    plain.numStreams = 1;
    plain.entrySizeBytes = 0x110;
    plain.streams[0].isoChannel = 5;
    plain.streams[0].hasSpeed = true;
    plain.streams[0].speed = 2;
    plain.streams[0].pcmChannels = 10;
    plain.streams[0].midiPorts = 1;

    StreamConfig ext{};
    ext.numStreams = 2;
    ext.streams[0].pcmChannels = 16;
    ext.streams[0].midiPorts = 1;
    std::strcpy(ext.streams[0].labels, "a\\\\");
    ext.streams[1].pcmChannels = 12;
    ext.streams[1].midiPorts = 0;
    std::strcpy(ext.streams[1].labels, "b\\\\");

    const StreamConfig merged = MergeExtensionStreamGeometry(plain, ext);

    EXPECT_EQ(merged.numStreams, 2U);
    EXPECT_EQ(merged.TotalPcmChannels(), 28U);
    // 16 PCM + one MIDI slot, then 12 PCM with no MIDI.
    EXPECT_EQ(merged.TotalAm824Slots(), 29U);

    // Only the plain section knows the wire identity; the extension carries none.
    EXPECT_EQ(merged.streams[0].isoChannel, 5);
    EXPECT_EQ(merged.streams[0].speed, 2U);
    EXPECT_TRUE(merged.streams[0].hasSpeed);
    EXPECT_STREQ(merged.streams[0].labels, "a\\\\");

    // A stream the plain section never reported starts unassigned, exactly like
    // a device-reported disabled stream: the planner assigns it later.
    EXPECT_EQ(merged.streams[1].isoChannel, -1);
    EXPECT_TRUE(merged.streams[1].hasSpeed);
    EXPECT_FALSE(merged.streams[1].hasSeqStart);
    EXPECT_STREQ(merged.streams[1].labels, "b\\\\");
}

TEST(DiceExtensionGeometry, MergeSynthesizesRxLayoutFlagsForNewStreams) {
    StreamConfig plain{};
    plain.isRxLayout = true;
    plain.numStreams = 1;
    plain.streams[0].isoChannel = 6;
    plain.streams[0].hasSeqStart = true;
    plain.streams[0].pcmChannels = 10;

    StreamConfig ext{};
    ext.numStreams = 2;
    ext.streams[0].pcmChannels = 16;
    ext.streams[1].pcmChannels = 12;

    const StreamConfig merged = MergeExtensionStreamGeometry(plain, ext);
    EXPECT_TRUE(merged.streams[1].hasSeqStart);
    EXPECT_FALSE(merged.streams[1].hasSpeed);
}

TEST(DiceExtensionGeometry, MergeKeepsThePlainReadWhenTheExtensionIsEmpty) {
    // A device that answers the CURRENT_CONFIG read with an empty block must
    // degrade to the plain handshake rather than publish zero channels.
    StreamConfig plain{};
    plain.numStreams = 1;
    plain.streams[0].isoChannel = 5;
    plain.streams[0].pcmChannels = 10;

    const StreamConfig merged = MergeExtensionStreamGeometry(plain, StreamConfig{});
    EXPECT_EQ(merged.numStreams, 1U);
    EXPECT_EQ(merged.TotalPcmChannels(), 10U);
    EXPECT_EQ(merged.streams[0].isoChannel, 5);
}

// ---------------------------------------------------------------------------
// End to end through DICETcatProtocol
// ---------------------------------------------------------------------------

TEST(DiceExtensionGeometry, ExtensionHandshakePublishesTheRateModeGeometry) {
    RegionFireWireBus bus;
    RouteState routeState;
    InstallDevice(bus, ExtensionShape::Present);

    const auto caps = LoadCaps(bus, routeState, /*preferExtension=*/true);

    // 16 + 12 each way, from the low rate mode's block -- not the 10 the plain
    // sections report for the mode the device is sitting in.
    EXPECT_EQ(caps.hostInputPcmChannels, 28U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 28U);
    EXPECT_EQ(caps.deviceToHostAm824Slots, 29U);
    EXPECT_EQ(caps.hostToDeviceAm824Slots, 29U);
    EXPECT_EQ(caps.sampleRateHz, 48000U);

    // Two streams per direction, with the plain section's iso channel kept on
    // the first and the second left for the planner.
    ASSERT_EQ(caps.deviceToHostStreamCount, 2U);
    ASSERT_EQ(caps.hostToDeviceStreamCount, 2U);
    EXPECT_EQ(caps.deviceToHostStreams[0].pcmChannels, 16U);
    EXPECT_EQ(caps.deviceToHostStreams[1].pcmChannels, 12U);
    EXPECT_EQ(caps.deviceToHostIsoChannel, 5);
    EXPECT_EQ(caps.hostToDeviceIsoChannel, 6);
    EXPECT_EQ(caps.deviceToHostStreams[1].isoChannel,
              ASFW::Audio::AudioStreamWireInfo::kInvalidIsoChannel);
    EXPECT_EQ(bus.unmappedReadCount, 0);
}

TEST(DiceExtensionGeometry, DegeneratePointerTableFallsBackToThePlainHandshake) {
    // The other field revision: the read succeeds, the table is an echo, and the
    // device must still come up on the plain sections rather than failing.
    RegionFireWireBus bus;
    RouteState routeState;
    InstallDevice(bus, ExtensionShape::Degenerate);

    const auto caps = LoadCaps(bus, routeState, /*preferExtension=*/true);

    EXPECT_EQ(caps.hostInputPcmChannels, 10U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 10U);
    EXPECT_EQ(caps.deviceToHostStreamCount, 1U);
    EXPECT_EQ(caps.hostToDeviceStreamCount, 1U);
}

TEST(DiceExtensionGeometry, CurrentConfigTooSmallForTheRateModeFallsBack) {
    RegionFireWireBus bus;
    RouteState routeState;
    InstallDevice(bus, ExtensionShape::CurrentConfigTooSmall);

    const auto caps = LoadCaps(bus, routeState, /*preferExtension=*/true);

    EXPECT_EQ(caps.hostInputPcmChannels, 10U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 10U);
}

TEST(DiceExtensionGeometry, ExtensionIsNotReadUnlessTheProfileOptsIn) {
    // The regression guard for every DICE model whose geometry is already
    // hardware-validated through the plain sections: an extension sitting on the
    // bus must not change what they publish.
    RegionFireWireBus bus;
    RouteState routeState;
    InstallDevice(bus, ExtensionShape::Present);

    const auto caps = LoadCaps(bus, routeState, /*preferExtension=*/false);

    EXPECT_EQ(caps.hostInputPcmChannels, 10U);
    EXPECT_EQ(caps.hostOutputPcmChannels, 10U);
    EXPECT_EQ(caps.deviceToHostStreamCount, 1U);
}
