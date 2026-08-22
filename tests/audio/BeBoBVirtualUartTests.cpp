// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// The mailbox conversation lives app-side (ASFW/DriverConnector+BeBoB.swift).
// What the driver still owns — and what these tests cover — is the guard that
// decides which commands a user client may put into the BridgeCo bootloader
// window at all.

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Families/BeBoB/VirtualUart/BeBoBVirtualUartCommand.hpp"

#include <array>
#include <cstdint>

namespace {

using namespace ASFW::Audio::Families::BeBoB::VirtualUart;

/// Wire bytes of a real request envelope, little-endian per FFADO
/// `bebob_dl_codes.cpp:52-64`: version, commandId lo/hi, commandCode, operandSize.
constexpr std::array<uint8_t, kCommandEnvelopeBytes> MakeEnvelope(uint16_t commandId,
                                                                 uint8_t opcode,
                                                                 uint8_t operandSize,
                                                                 uint32_t operand) {
    return {
        0x01, 0x00, 0x00, 0x00,
        static_cast<uint8_t>(commandId & 0xFF), static_cast<uint8_t>(commandId >> 8),
        opcode, operandSize,
        static_cast<uint8_t>(operand & 0xFF), static_cast<uint8_t>((operand >> 8) & 0xFF),
        static_cast<uint8_t>((operand >> 16) & 0xFF), static_cast<uint8_t>((operand >> 24) & 0xFF),
    };
}

TEST(BeBoBVirtualUartTests, OpcodeIsReadFromTheThirdByteOfQuadletOne) {
    // Byte 6 is the command code. Reading it from anywhere else would let a
    // flash-programming opcode past the guard while a shell opcode sat in the
    // bytes actually inspected.
    const auto poll = MakeEnvelope(0x1234, 0x08, 1, 1024);
    EXPECT_EQ(VirtualUartOpcodeOf(poll), 0x08U);

    const auto programGuid = MakeEnvelope(0x1234, 0x0A, 2, 0);
    EXPECT_EQ(VirtualUartOpcodeOf(programGuid), 0x0AU);

    // A commandId or operand that happens to look like a permitted opcode must
    // not be mistaken for one.
    const auto decoy = MakeEnvelope(0x0908, 0x0A, 2, 0x0007'0908);
    EXPECT_EQ(VirtualUartOpcodeOf(decoy), 0x0AU);
}

TEST(BeBoBVirtualUartTests, SafetyGuardRejectsDestructiveOpcodes) {
    // Permitted shell opcodes
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x07));
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x08));
    EXPECT_TRUE(IsPermittedVirtualUartOpcode(0x09));

    // Flash / GUID destructive bootloader opcodes
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x01)); // Halt
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x02)); // Reset
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x04)); // DownloadStart
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x05)); // DownloadBlock
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x06)); // DownloadEnd
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0a)); // ProgramGUID
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0b)); // ProgramMAC
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0c)); // InitPersParams
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x0d)); // InitConfigToFactorySetting
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x10)); // ProgramHWIdVersion
    EXPECT_FALSE(IsPermittedVirtualUartOpcode(0x11)); // Bootloader Cue (Go)
}

TEST(BeBoBVirtualUartTests, MailboxAddressesMatchTheBridgeCoWindow) {
    // Cross-checked against FFADO bebob_dl_mgr.cpp:45-49 and Linux
    // bebob.h:38-39 (BEBOB_ADDR_REG_REQ = 0xffffc8021000).
    EXPECT_EQ(kVirtualUartAddressHi, 0xFFFFU);
    EXPECT_EQ(kRequestAddressLo, 0xC802'1000U);
    EXPECT_EQ(kRequestBufferAddressLo, 0xC802'1040U);
    EXPECT_EQ(kResponseAddressLo, 0xC802'9000U);
    EXPECT_EQ(kResponseBufferAddressLo, 0xC802'9040U);
}

} // namespace
