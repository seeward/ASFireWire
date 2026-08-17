// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBStreamTelemetryParser.hpp"
#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBTelemetryTypes.hpp"
#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBVirtualUartCommand.hpp"

namespace {

using namespace ASFW::Audio::Families::BeBoB::VirtualUart;

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

} // namespace
