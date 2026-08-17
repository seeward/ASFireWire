// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBStreamTelemetryParser.hpp"
#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBTelemetryTypes.hpp"
#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBVirtualUartClient.hpp"
#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBVirtualUartCommand.hpp"

#include "DeferredFireWireBus.hpp"
#include "FakeTimerScheduler.hpp"

#include <memory>
#include <string>

namespace {

using namespace ASFW::Audio::Families::BeBoB::VirtualUart;

/// A route the client will accept: operational node, non-zero epoch.
ASFW::Discovery::DeviceRouteToken MakeRoute() {
    ASFW::Discovery::DeviceRouteToken route{};
    route.deviceInstanceId = ASFW::Discovery::DeviceInstanceId{1};
    route.routeEpoch = 1;
    route.generation = ASFW::FW::Generation{1};
    route.nodeId = 2;
    return route;
}

struct UartFixture {
    ASFW::Async::Testing::DeferredFireWireBus bus{};
    ASFW::Testing::FakeTimerScheduler timers{};
    std::shared_ptr<BeBoBVirtualUartClient> client;

    UartFixture() {
        bus.SetGeneration(ASFW::FW::Generation{1});
        client = std::make_shared<BeBoBVirtualUartClient>(bus, MakeRoute(), &timers);
    }
};

TEST(BeBoBVirtualUartTests, CommandEnvelopeEncodesLittleEndian) {
    // 0x07: SwitchTo1394Shell
    const auto switchCmd = MakeSwitchToShellCommand(1, 0x1234);
    const auto bytes = switchCmd.Bytes();
    ASSERT_EQ(bytes.size(), 12U);

    // Quadlet 0: Protocol Version = 1
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[1], 0x00);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);

    // Quadlet 1: commandId=0x1234, opcode=0x07, operandSize=0
    EXPECT_EQ(bytes[4], 0x34);
    EXPECT_EQ(bytes[5], 0x12);
    EXPECT_EQ(bytes[6], 0x07);
    EXPECT_EQ(bytes[7], 0x00);

    // Quadlet 2: operand=0
    EXPECT_EQ(bytes[8], 0x00);
    EXPECT_EQ(bytes[9], 0x00);
    EXPECT_EQ(bytes[10], 0x00);
    EXPECT_EQ(bytes[11], 0x00);

    EXPECT_EQ(switchCmd.ProtocolVersion(), 1U);
    EXPECT_EQ(switchCmd.CommandId(), 0x1234U);
    EXPECT_EQ(switchCmd.Opcode(), VirtualUartOpcode::kSwitchTo1394Shell);
}

TEST(BeBoBVirtualUartTests, WriteShellCharsEncodesLengthOperand) {
    // 0x09: WriteShellChars length=258 (0x0102)
    const auto writeCmd = MakeWriteShellCharsCommand(1, 42, 258);
    const auto bytes = writeCmd.Bytes();

    EXPECT_EQ(bytes[6], 0x09); // Opcode
    EXPECT_EQ(bytes[7], 0x01); // 1 operand quadlet
    EXPECT_EQ(bytes[8], 0x02); // Length low byte
    EXPECT_EQ(bytes[9], 0x01); // Length high byte
    EXPECT_EQ(bytes[10], 0x00);
    EXPECT_EQ(bytes[11], 0x00);
}

TEST(BeBoBVirtualUartTests, SafetyGuardRejectsDestructiveOpcodes) {
    // Permitted shell opcodes
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x07));
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x08));
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x09));

    // Flash / GUID destructive bootloader opcodes
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x04)); // DownloadStart
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x05)); // DownloadBlock
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x06)); // DownloadEnd
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0a)); // ProgramGUID
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0b)); // ProgramMAC
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x10)); // ProgramHWIdVersion
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x11)); // Bootloader Cue (Go)
}

TEST(BeBoBVirtualUartTests, DecodeResponseEnvelope) {
    std::array<uint8_t, 12> rawResponse{
        0x01, 0x00, 0x00, 0x00, // Version 1
        0x05, 0x00, 0x08, 0x01, // CmdId=5, Opcode=0x08, OperandSize=1
        0x80, 0x01, 0x00, 0x00  // Available bytes = 384 (0x0180)
    };

    const auto decoded = VirtualUartResponseEnvelope::Decode(rawResponse);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protocolVersion, 1U);
    EXPECT_EQ(decoded->commandId, 5U);
    EXPECT_EQ(decoded->opcode, 0x08U);
    EXPECT_EQ(decoded->operand, 384U);
}

TEST(BeBoBVirtualUartTests, ParseStreamingStatsStdout) {
    constexpr std::string_view sampleSysStat =
        "rxPackets:       12450\n"
        "onlyHeaders:      3112\n"
        "rxEmptyPkt:          0\n"
        "rxNoMem:             0\n"
        "rxQFillLevel:       15 %\n"
        "PoolFillLevel:      85 %\n"
        "CtrDiffErr:          0\n"
        "SytDiffErr:          0\n"
        "BCOHdrErr:           0\n"
        "pkt Future:          0\n"
        "pkt Past:            0\n"
        "pktSytDiff:         -4\n"
        "SytOffset:        4096\n"
        "SytCorr:             2\n";

    const auto stats = BeBoBStreamTelemetryParser::ParseStreamingStats(sampleSysStat);
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->rxPackets, 12450U);
    EXPECT_EQ(stats->onlyHeaders, 3112U);
    EXPECT_EQ(stats->rxQFillLevelPct, 15U);
    EXPECT_EQ(stats->poolFillLevelPct, 85U);
    EXPECT_EQ(stats->bcoHdrErr, 0U);
    EXPECT_EQ(stats->sytDiffErr, 0U);
    EXPECT_EQ(stats->pktSytDiff, -4);
    EXPECT_EQ(stats->sytOffset, 4096U);
    EXPECT_EQ(stats->sytCorr, 2);
}

TEST(BeBoBVirtualUartTests, ParseAvStatStdout) {
    constexpr std::string_view sampleAvStat =
        "TGEN in lock: SetTgInLock\n"
        "Framer Status: CIPMismatch: 0, DBCMismatch: 0\n";

    const auto av = BeBoBStreamTelemetryParser::ParseAvStat(sampleAvStat);
    ASSERT_TRUE(av.has_value());
    EXPECT_TRUE(av->setTgInLock);
    EXPECT_FALSE(av->cipMismatch);
    EXPECT_FALSE(av->dbcMismatch);
}

TEST(BeBoBVirtualUartTests, ParseSyncStateStdout) {
    constexpr std::string_view sampleSyncShow =
        "Sampling Frequency = 48kHz\n"
        "Sync Source        = Internal Sync\n"
        "Audio State        = Running\n"
        "Selected Iso Channels:\n"
        "  LineIn .....     = 8\n"
        "  SpdifAdatIn .... = 2\n"
        "  SpdifAdatOut ... = 2\n"
        "  MixerOut ...     = 8\n";

    const auto sync = BeBoBStreamTelemetryParser::ParseSyncState(sampleSyncShow);
    ASSERT_TRUE(sync.has_value());
    EXPECT_EQ(sync->sampleRateHz, 48000U);
    EXPECT_EQ(sync->syncSource, BeBoBSyncSource::kInternal);
    EXPECT_EQ(sync->audioState, BeBoBAudioState::kRunning);
    EXPECT_EQ(sync->lineInChannels, 8U);
}

// The DM1000 mailbox is single-occupancy. Two overlapping conversations are
// answered with rCode 4 and, worse, a drain that loses its request/response
// pairing re-serves the same 128-byte page indefinitely. The client is the sole
// owner precisely so callers cannot do this to each other.
TEST(BeBoBVirtualUartTests, SecondCommandQueuesInsteadOfSharingTheMailbox) {
    UartFixture fixture;

    bool firstDone = false;
    bool secondDone = false;
    fixture.client->ExecuteCommand("sys stat", [&](std::string) { firstDone = true; });
    fixture.client->ExecuteCommand("fw show", [&](std::string) { secondDone = true; });

    EXPECT_TRUE(fixture.client->Busy());
    EXPECT_EQ(fixture.client->QueueDepth(), 1U) << "second command must wait";
    EXPECT_FALSE(firstDone);
    EXPECT_FALSE(secondDone);

    // Only the first conversation may have touched the bus. Its opening move is
    // the pre-drain's 0x08 request; the second command must not have written.
    const size_t writesFromFirst = fixture.bus.WriteCount();
    EXPECT_GT(writesFromFirst, 0U);
    for (size_t index = 0; index < writesFromFirst; ++index) {
        EXPECT_EQ(fixture.bus.WriteAt(index).address.addressLo & 0xFFFFF000U, 0xC8021000U)
            << "write " << index << " left the mailbox window";
    }
}

// "help\r\n" is six bytes. Writing it unpadded drops the CR/LF, leaving the two
// stale bytes the previous command left at those offsets, so the device echoes
// a garbled line and never executes it. The envelope must still declare six.
TEST(BeBoBVirtualUartTests, RequestBufferWriteIsQuadletPaddedButOperandIsNot) {
    UartFixture fixture;
    fixture.client->WriteChars("help\r\n", [](bool) {});

    ASSERT_GE(fixture.bus.PendingWriteCount(), 1U);
    const auto& bufferWrite = fixture.bus.PendingWriteAt(0);
    EXPECT_EQ(bufferWrite.address.addressLo, 0xC8021040U) << "request data buffer";
    EXPECT_EQ(bufferWrite.data.size(), 8U) << "6 bytes padded up to a quadlet boundary";
    EXPECT_EQ(bufferWrite.data[4], '\r') << "terminator must survive the write";
    EXPECT_EQ(bufferWrite.data[5], '\n');
    EXPECT_EQ(bufferWrite.data[6], 0U) << "padding only";
    EXPECT_EQ(bufferWrite.data[7], 0U);

    // Releasing the buffer write lets the envelope follow; its operand is the
    // true unpadded length, so the device consumes exactly the command.
    ASSERT_TRUE(fixture.bus.CompleteNextWrite(ASFW::Async::AsyncStatus::kSuccess));
    ASSERT_GE(fixture.bus.PendingWriteCount(), 1U);
    const auto& envelopeWrite = fixture.bus.PendingWriteAt(0);
    EXPECT_EQ(envelopeWrite.address.addressLo, 0xC8021000U) << "request envelope";
    ASSERT_EQ(envelopeWrite.data.size(), 12U);
    EXPECT_EQ(envelopeWrite.data[6], 0x09) << "kWriteShellChars";
    EXPECT_EQ(envelopeWrite.data[8], 6U) << "operand is the unpadded length";
    EXPECT_EQ(envelopeWrite.data[9], 0U);
}

} // namespace
