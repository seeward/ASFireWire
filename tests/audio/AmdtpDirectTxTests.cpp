#include "Audio/Engine/Direct/Tx/DiceTxStreamEngine.hpp"
#include "Audio/Ports/IAmdtpTxSlotProvider.hpp"
#include "Audio/Wire/AMDTP/AmdtpPacketTimeline.hpp"
#include "Audio/Wire/AMDTP/AmdtpPayloadWriter.hpp"
#include "Audio/Wire/AMDTP/AmdtpTxPacketizer.hpp"
#include "Audio/Wire/AMDTP/PcmSlotCodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace {

using namespace ASFW::Protocols::Audio::AMDTP;
using ASFW::Protocols::Audio::DICE::DiceTxStreamEngine;
using ASFW::Protocols::Audio::DICE::TxSlotPrepareResult;

class TestTxSlotProvider final : public IAmdtpTxSlotProvider {
public:
    bool allowAcquire{false};
    bool allowPublish{false};
    std::array<uint8_t, 128> bytes{};

    bool AcquireWritableSlot(
        uint32_t packetIndex,
        TxPacketSlotView& outSlot) noexcept override {
        if (!allowAcquire) {
            return false;
        }
        outSlot = {
            .packetIndex = packetIndex,
            .bytes = bytes.data(),
            .capacityBytes =
                static_cast<uint32_t>(bytes.size()),
        };
        return true;
    }

    bool PublishSlot(
        const PreparedTxPacket&) noexcept override {
        return allowPublish;
    }

    uint32_t SlotCount() const noexcept override {
        return 1;
    }
};

AmdtpStreamConfig BlockingStereoConfig() {
    AmdtpStreamConfig config{};
    config.streamMode = StreamMode::Blocking;
    config.dbs = 2;
    config.pcmChannels = 2;
    config.framesPerDataPacket = 8;
    config.maxPacketBytes = 128;
    return config;
}

class AmdtpPacketDefaultsTests : public testing::TestWithParam<PcmSlotEncoding> {
protected:
    void SetUp() override {
        config_.pcmChannels = 10;
        config_.midiSlots = 1;
        config_.dbs = 11;
        policy_.hostToDevicePcmEncoding = GetParam();
        ASSERT_TRUE(timeline_.AttachSlots(timelineSlots_.data(), timelineSlots_.size()));
        packetizer_.BindTimeline(&timeline_);
        ASSERT_TRUE(packetizer_.Configure(config_, policy_));
        bytes_.fill(0xA5);
    }

    bool PrepareData(uint32_t packetIndex) {
        AmdtpTimingState timing{};
        timing.txClockValid = true;
        timing.disposition = AmdtpPacketDisposition::Data;
        timing.nextDataSyt = 0x1234;
        timing.replayValid = true;
        timing.replayDataBlocks = 8;
        return packetizer_.PrepareNextPacket(
            {packetIndex, bytes_.data(), static_cast<uint32_t>(bytes_.size())},
            timing, packet_);
    }

    void ExpectSilentPayload() {
        ASSERT_TRUE(packet_.isData);
        ASSERT_EQ(packet_.byteCount, 360U);
        ASSERT_EQ(packet_.framesInPacket, 8U);
        ASSERT_EQ(packet_.dbs, 11U);
        const uint8_t pcmLabel = GetParam() == PcmSlotEncoding::Am824MBLA ? 0x40 : 0;
        for (uint32_t frame = 0; frame < 8; ++frame) {
            for (uint32_t channel = 0; channel < 11; ++channel) {
                SCOPED_TRACE(testing::Message() << "frame=" << frame << " channel=" << channel);
                const uint32_t offset = 8 + (frame * 11 + channel) * 4;
                EXPECT_EQ(bytes_[offset], channel < 10 ? pcmLabel : 0x80);
                EXPECT_EQ(bytes_[offset + 1], 0);
                EXPECT_EQ(bytes_[offset + 2], 0);
                EXPECT_EQ(bytes_[offset + 3], 0);
            }
        }
        for (uint32_t offset = packet_.byteCount; offset < bytes_.size(); ++offset) {
            EXPECT_EQ(bytes_[offset], 0xA5) << "beyond packet at " << offset;
        }
    }

    AmdtpStreamConfig config_{};
    AmdtpTxPolicy policy_{};
    AmdtpPacketTimeline timeline_{};
    std::array<PacketTimelineSlot, 4> timelineSlots_{};
    AmdtpTxPacketizer packetizer_{};
    std::array<uint8_t, 512> bytes_{};
    PreparedTxPacket packet_{};
};

TEST_P(AmdtpPacketDefaultsTests, UnwrittenDataPacketContainsWireSilence) {
    ASSERT_TRUE(PrepareData(0));
    ExpectSilentPayload();
    EXPECT_EQ(bytes_[1], 11); // Constant DBS includes the MIDI slot.
    EXPECT_EQ(bytes_[4], 0x90);
    EXPECT_EQ(bytes_[5], 0x02);
    EXPECT_EQ(bytes_[6], 0x12);
    EXPECT_EQ(bytes_[7], 0x34);
}

TEST_P(AmdtpPacketDefaultsTests, ReusedDataPacketRestoresSilenceAfterHostAudio) {
    ASSERT_TRUE(PrepareData(0));
    AmdtpPayloadWriter writer{};
    writer.Configure(config_, policy_);
    writer.BindTimeline(&timeline_);
    std::array<float, 10> hostFrame{0.5f, -0.5f};
    writer.WriteFloat32Interleaved({hostFrame.data(), 0, 1, 1, 10}, 0);

    // Golden wire bytes: both PCM polarities retain the selected encoding.
    std::array<uint8_t, 8> expected{};
    switch (GetParam()) {
    case PcmSlotEncoding::Am824MBLA:
        expected = {0x40, 0x40, 0, 0, 0x40, 0xC0, 0, 0};
        break;
    case PcmSlotEncoding::RawSigned24In32BE:
        expected = {0, 0x40, 0, 0, 0xFF, 0xC0, 0, 0};
        break;
    case PcmSlotEncoding::RawSigned24In32LE:
        expected = {0, 0, 0x40, 0, 0, 0, 0xC0, 0xFF};
        break;
    }
    for (uint32_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(bytes_[8 + i], expected[i]) << "PCM byte " << i;
    }
    EXPECT_EQ(bytes_[8 + 10 * 4], 0x80); // PCM writes leave MIDI alone.

    // A subsequent packet in the same memory must not replay the old audio
    // when the host writer misses its opportunity to fill the new packet.
    ASSERT_TRUE(PrepareData(1));
    EXPECT_EQ(packet_.dbc, 8);
    EXPECT_EQ(packet_.firstAudioFrame, 8U);
    ExpectSilentPayload();
}

TEST_P(AmdtpPacketDefaultsTests, NoDataRemainsHeaderOnlyAndDoesNotTouchPayload) {
    AmdtpTimingState timing{};
    timing.disposition = AmdtpPacketDisposition::NoData;
    ASSERT_TRUE(packetizer_.PrepareNextPacket(
        {0, bytes_.data(), static_cast<uint32_t>(bytes_.size())}, timing, packet_));
    EXPECT_FALSE(packet_.isData);
    EXPECT_EQ(packet_.byteCount, 8U);
    EXPECT_EQ(packet_.framesInPacket, 0U);
    EXPECT_EQ(packet_.dbc, 0);
    EXPECT_EQ(bytes_[1], 11);
    EXPECT_EQ(bytes_[4], 0x90);
    EXPECT_EQ(bytes_[5], 0xFF);
    EXPECT_EQ(bytes_[6], 0xFF);
    EXPECT_EQ(bytes_[7], 0xFF);
    for (uint32_t offset = 8; offset < bytes_.size(); ++offset) {
        EXPECT_EQ(bytes_[offset], 0xA5) << "payload byte " << offset;
    }
}

INSTANTIATE_TEST_SUITE_P(
    PcmEncodings, AmdtpPacketDefaultsTests,
    testing::Values(PcmSlotEncoding::Am824MBLA,
                    PcmSlotEncoding::RawSigned24In32BE,
                    PcmSlotEncoding::RawSigned24In32LE));

TEST(AmdtpDirectTxTests, Int32EncodingUsesHighSigned24Bits) {
    EXPECT_EQ(PcmSlotCodec::EncodeInt32(
                  INT32_MAX, PcmSlotEncoding::RawSigned24In32BE),
              0x007FFFFFu);
    EXPECT_EQ(PcmSlotCodec::EncodeInt32(
                  INT32_MIN, PcmSlotEncoding::RawSigned24In32BE),
              0xFF800000u);
    EXPECT_EQ(PcmSlotCodec::EncodeInt32(
                  INT32_MAX, PcmSlotEncoding::Am824MBLA),
              0x407FFFFFu);
}

TEST(AmdtpDirectTxTests, ForcedNoDataHoldsDbcAndAudioFrame) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 8> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(
        timelineSlots.data(), timelineSlots.size()));

    AmdtpTxPacketizer packetizer{};
    packetizer.BindTimeline(&timeline);
    ASSERT_TRUE(packetizer.Configure(
        BlockingStereoConfig(), AmdtpTxPolicy{}));

    std::array<std::array<uint8_t, 128>, 3> bytes{};
    PreparedTxPacket first{};
    PreparedTxPacket forced{};
    PreparedTxPacket data{};

    AmdtpTimingState allowData{};
    allowData.txClockValid = true;
    allowData.disposition = AmdtpPacketDisposition::Data;
    allowData.nextDataSyt = 0x1234;
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, bytes[0].data(), bytes[0].size()}, allowData, first));
    EXPECT_FALSE(first.isData);
    EXPECT_EQ(first.byteCount, 8U);
    EXPECT_EQ(bytes[0][4], 0x90);
    EXPECT_EQ(bytes[0][5], 0xFF);
    EXPECT_EQ(bytes[0][6], 0xFF);
    EXPECT_EQ(bytes[0][7], 0xFF);

    AmdtpTimingState noData{};
    noData.disposition = AmdtpPacketDisposition::NoData;
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {1, bytes[1].data(), bytes[1].size()}, noData, forced));
    EXPECT_FALSE(forced.isData);
    EXPECT_EQ(forced.byteCount, 8U);
    EXPECT_EQ(forced.dbc, first.dbc);
    EXPECT_EQ(forced.firstAudioFrame, 0U);

    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {2, bytes[2].data(), bytes[2].size()}, allowData, data));
    EXPECT_TRUE(data.isData);
    EXPECT_EQ(data.dbc, first.dbc);
    EXPECT_EQ(data.firstAudioFrame, 0U);
    EXPECT_EQ(data.framesInPacket, 8U);
    EXPECT_EQ(data.syt, 0x1234U);
}

TEST(AmdtpDirectTxTests, NoDataFdfCanUseSaffireCompatibilityQuirk) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 4> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(
        timelineSlots.data(), timelineSlots.size()));

    AmdtpTxPolicy policy{};
    policy.preserveFdfInNoDataPackets = true;

    AmdtpTxPacketizer packetizer{};
    packetizer.BindTimeline(&timeline);
    ASSERT_TRUE(packetizer.Configure(BlockingStereoConfig(), policy));

    std::array<uint8_t, 128> bytes{};
    AmdtpTimingState timing{};
    timing.disposition = AmdtpPacketDisposition::NoData;
    PreparedTxPacket packet{};
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, bytes.data(), bytes.size()}, timing, packet));

    ASSERT_FALSE(packet.isData);
    EXPECT_EQ(bytes[4], 0x90);
    EXPECT_EQ(bytes[5], 0x02);
    EXPECT_EQ(bytes[6], 0xFF);
    EXPECT_EQ(bytes[7], 0xFF);
}

TEST(AmdtpDirectTxTests, ReplayOverridesLocalCadencePerPhysicalCycle) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 8> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(
        timelineSlots.data(), timelineSlots.size()));

    AmdtpTxPacketizer packetizer{};
    packetizer.BindTimeline(&timeline);
    ASSERT_TRUE(packetizer.Configure(
        BlockingStereoConfig(), AmdtpTxPolicy{}));

    std::array<std::array<uint8_t, 128>, 2> bytes{};
    AmdtpTimingState data{};
    data.txClockValid = true;
    data.disposition = AmdtpPacketDisposition::Data;
    data.nextDataSyt = 0x2345;
    data.replayValid = true;
    data.replayDataBlocks = 8;

    PreparedTxPacket packet{};
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, bytes[0].data(), bytes[0].size()}, data, packet));
    EXPECT_TRUE(packet.isData);
    EXPECT_EQ(packet.framesInPacket, 8U);
    EXPECT_EQ(packet.syt, 0x2345U);

    AmdtpTimingState noData{};
    noData.disposition = AmdtpPacketDisposition::NoData;
    noData.replayValid = true;
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {1, bytes[1].data(), bytes[1].size()}, noData, packet));
    EXPECT_FALSE(packet.isData);
    EXPECT_EQ(packet.framesInPacket, 0U);
    EXPECT_EQ(packet.dbc, 8U);
}

TEST(AmdtpDirectTxTests, PayloadWriterReadsMappedInt32RingDirectly) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 8> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(
        timelineSlots.data(), timelineSlots.size()));

    AmdtpTxPacketizer packetizer{};
    packetizer.BindTimeline(&timeline);
    const auto config = BlockingStereoConfig();
    ASSERT_TRUE(packetizer.Configure(config, AmdtpTxPolicy{}));

    std::array<uint8_t, 128> noDataBytes{};
    std::array<uint8_t, 128> dataBytes{};
    AmdtpTimingState timing{};
    timing.txClockValid = true;
    timing.disposition = AmdtpPacketDisposition::Data;
    timing.nextDataSyt = 0x2222;
    PreparedTxPacket packet{};
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, noDataBytes.data(), noDataBytes.size()}, timing, packet));
    ASSERT_FALSE(packet.isData);
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {1, dataBytes.data(), dataBytes.size()}, timing, packet));
    ASSERT_TRUE(packet.isData);

    AmdtpPayloadWriter writer{};
    writer.Configure(config, AmdtpTxPolicy{});
    writer.BindTimeline(&timeline);
    std::array<float, 16> mappedRing{};
    mappedRing[0] = 1.0f;
    mappedRing[1] = -1.0f;
    // completionCursor 0: no packet counts as already transmitted here.
    writer.WriteFloat32Interleaved(
        {mappedRing.data(), 0, 8, 8, 2}, 0);

    EXPECT_EQ(dataBytes[8], 0x40);
    EXPECT_EQ(dataBytes[9], 0x7F);
    EXPECT_EQ(dataBytes[10], 0xFF);
    EXPECT_EQ(dataBytes[11], 0xFF);
    EXPECT_EQ(dataBytes[12], 0x40);
    EXPECT_EQ(dataBytes[13], 0x80);
    EXPECT_EQ(dataBytes[14], 0x00);
    EXPECT_EQ(dataBytes[15], 0x01);
}

TEST(AmdtpDirectTxTests, PayloadWriterCountsUnderExposureAtCallBoundary) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 4> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(
        timelineSlots.data(), timelineSlots.size()));

    AmdtpPayloadWriter writer{};
    writer.Configure(BlockingStereoConfig(), AmdtpTxPolicy{});
    writer.BindTimeline(&timeline);

    std::array<float, 16> mappedRing{};
    writer.WriteFloat32Interleaved(
        {mappedRing.data(), 0, 8, 8, 2}, 0);

    const auto& counters = writer.Counters();
    EXPECT_EQ(
        counters.underExposureCalls.load(std::memory_order_relaxed), 1U);
    EXPECT_EQ(
        counters.underExposureFrames.load(std::memory_order_relaxed), 8U);
    EXPECT_EQ(
        counters.framesWithoutPacket.load(std::memory_order_relaxed), 8U);
}

TEST(AmdtpDirectTxTests, AlignFrameCursorIsAcceptedOnlyOncePerReset) {
    AmdtpTxPacketizer packetizer{};

    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 8> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(timelineSlots.data(), timelineSlots.size()));
    packetizer.BindTimeline(&timeline);
    ASSERT_TRUE(packetizer.Configure(BlockingStereoConfig(), AmdtpTxPolicy{}));
    EXPECT_TRUE(packetizer.AlignFrameCursorOnce(960U));
    EXPECT_FALSE(packetizer.AlignFrameCursorOnce(2000U));

    std::array<std::array<uint8_t, 128>, 3> bytes{};
    PreparedTxPacket packet{};
    AmdtpTimingState timing{};
    timing.txClockValid = true;
    timing.disposition = AmdtpPacketDisposition::Data;
    timing.nextDataSyt = 0x1234;

    // Packet index 0: not data in cadence
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, bytes[0].data(), bytes[0].size()}, timing, packet));
    EXPECT_FALSE(packet.isData);

    // Packet index 1: data in cadence
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {1, bytes[1].data(), bytes[1].size()}, timing, packet));
    EXPECT_TRUE(packet.isData);
    EXPECT_EQ(packet.firstAudioFrame, 960U);

    // Packet index 2: data in cadence
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {2, bytes[2].data(), bytes[2].size()}, timing, packet));
    EXPECT_TRUE(packet.isData);
    EXPECT_EQ(packet.firstAudioFrame, 968U);

    packetizer.Reset(0, 0);
    EXPECT_TRUE(packetizer.AlignFrameCursorOnce(3000U));
}

TEST(AmdtpDirectTxTests, PacketizerTelemetryTracksCursorAlignmentAndLastDataRange) {
    AmdtpPacketTimeline timeline{};
    std::array<PacketTimelineSlot, 8> timelineSlots{};
    ASSERT_TRUE(timeline.AttachSlots(timelineSlots.data(), timelineSlots.size()));

    AmdtpTxPacketizer packetizer{};
    packetizer.BindTimeline(&timeline);
    ASSERT_TRUE(packetizer.Configure(BlockingStereoConfig(), AmdtpTxPolicy{}));
    EXPECT_TRUE(packetizer.AlignFrameCursorOnce(960U));

    std::array<std::array<uint8_t, 128>, 2> bytes{};
    AmdtpTimingState timing{};
    timing.txClockValid = true;
    timing.disposition = AmdtpPacketDisposition::Data;
    timing.nextDataSyt = 0x1234;
    PreparedTxPacket packet{};
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {0, bytes[0].data(), bytes[0].size()}, timing, packet));
    ASSERT_FALSE(packet.isData);
    ASSERT_TRUE(packetizer.PrepareNextPacket(
        {1, bytes[1].data(), bytes[1].size()}, timing, packet));
    ASSERT_TRUE(packet.isData);

    const auto snapshot = packetizer.TelemetrySnapshot();
    EXPECT_TRUE(snapshot.frameCursorAligned);
    EXPECT_TRUE(snapshot.hasLastDataPacket);
    EXPECT_EQ(snapshot.nextAudioFrame, 968U);
    EXPECT_EQ(snapshot.lastDataFirstAudioFrame, 960U);
    EXPECT_EQ(snapshot.lastDataEndAudioFrame, 968U);
    EXPECT_EQ(snapshot.lastDataPacketIndex, 1U);

    packetizer.ReArmFrameCursorAlignment();
    const auto rearmed = packetizer.TelemetrySnapshot();
    EXPECT_FALSE(rearmed.frameCursorAligned);
    EXPECT_GT(rearmed.cursorEpoch, snapshot.cursorEpoch);
}

TEST(AmdtpDirectTxTests, TxEngineReportsPreparationFailureStage) {
    DiceTxStreamEngine engine{};
    AmdtpTimingState timing{};

    EXPECT_EQ(
        engine.PrepareNextTransmitSlot(192, timing),
        TxSlotPrepareResult::kSlotProviderUnavailable);

    TestTxSlotProvider provider{};
    engine.BindSlotProvider(&provider);
    EXPECT_EQ(
        engine.PrepareNextTransmitSlot(192, timing),
        TxSlotPrepareResult::kSlotAcquireFailed);

    provider.allowAcquire = true;
    EXPECT_EQ(
        engine.PrepareNextTransmitSlot(192, timing),
        TxSlotPrepareResult::kPacketizerRejected);
}

} // namespace
