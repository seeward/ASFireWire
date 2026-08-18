// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBVirtualUartCommand.hpp — Safe, strongly-typed 1394 Virtual UART command framing.
//
// Only shell opcodes (0x07, 0x08, 0x09) can be constructed. Destructive flash-writing
// and GUID-reprogramming opcodes are blocked at the type level.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

// Register offsets in IEEE 1394 48-bit address space
inline constexpr uint16_t kVirtualUartAddressHi = 0xFFFF;
inline constexpr uint32_t kRequestAddressLo       = 0xC802'1000;
inline constexpr uint32_t kRequestBufferAddressLo = 0xC802'1040;
inline constexpr uint32_t kResponseAddressLo      = 0xC802'9000;
inline constexpr uint32_t kResponseBufferAddressLo = 0xC802'9040;

inline constexpr size_t kCommandEnvelopeBytes = 12;

enum class VirtualUartOpcode : uint8_t {
    kSwitchTo1394Shell = 0x07,
    kReadShellChars    = 0x08,
    kWriteShellChars   = 0x09,
};

[[nodiscard]] constexpr bool IsPermittedVirtualUartOpcode(uint8_t opcode) noexcept {
    return opcode == static_cast<uint8_t>(VirtualUartOpcode::kSwitchTo1394Shell) ||
           opcode == static_cast<uint8_t>(VirtualUartOpcode::kReadShellChars) ||
           opcode == static_cast<uint8_t>(VirtualUartOpcode::kWriteShellChars);
}

/// Helper to write little-endian quadlet to byte array.
constexpr void StoreLittleEndianQuadlet(std::span<uint8_t> dest, size_t offset, uint32_t value) noexcept {
    dest[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    dest[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dest[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

/// Helper to load little-endian quadlet from byte array.
[[nodiscard]] constexpr uint32_t LoadLittleEndianQuadlet(std::span<const uint8_t> src, size_t offset) noexcept {
    return static_cast<uint32_t>(src[offset]) |
           (static_cast<uint32_t>(src[offset + 1]) << 8U) |
           (static_cast<uint32_t>(src[offset + 2]) << 16U) |
           (static_cast<uint32_t>(src[offset + 3]) << 24U);
}

/// Request command envelope sent to 0xFFFF_C802_1000.
class VirtualUartEnvelope final {
public:
    constexpr VirtualUartEnvelope(uint32_t protocolVersion,
                                  uint16_t commandId,
                                  VirtualUartOpcode opcode,
                                  uint8_t operandSize,
                                  uint32_t operand) noexcept {
        StoreLittleEndianQuadlet(bytes_, 0, protocolVersion);
        const uint32_t q1 = (static_cast<uint32_t>(operandSize) << 24U) |
                            (static_cast<uint32_t>(opcode) << 16U) |
                            static_cast<uint32_t>(commandId);
        StoreLittleEndianQuadlet(bytes_, 4, q1);
        StoreLittleEndianQuadlet(bytes_, 8, operand);
    }

    [[nodiscard]] std::span<const uint8_t> Bytes() const noexcept {
        return std::span<const uint8_t>{bytes_.data(), bytes_.size()};
    }

    [[nodiscard]] uint32_t ProtocolVersion() const noexcept {
        return LoadLittleEndianQuadlet(bytes_, 0);
    }

    [[nodiscard]] uint16_t CommandId() const noexcept {
        return static_cast<uint16_t>(LoadLittleEndianQuadlet(bytes_, 4) & 0xFFFF);
    }

    [[nodiscard]] VirtualUartOpcode Opcode() const noexcept {
        return static_cast<VirtualUartOpcode>((LoadLittleEndianQuadlet(bytes_, 4) >> 16U) & 0xFF);
    }

private:
    std::array<uint8_t, kCommandEnvelopeBytes> bytes_{};
};

/// 0x07: SwitchTo1394Shell
[[nodiscard]] inline VirtualUartEnvelope MakeSwitchToShellCommand(
    uint32_t protocolVersion, uint16_t commandId) noexcept {
    return VirtualUartEnvelope{protocolVersion, commandId, VirtualUartOpcode::kSwitchTo1394Shell, 0, 0};
}

/// 0x09: WriteShellChars (commit bytes written to buffer)
[[nodiscard]] inline VirtualUartEnvelope MakeWriteShellCharsCommand(
    uint32_t protocolVersion, uint16_t commandId, uint32_t byteCount) noexcept {
    return VirtualUartEnvelope{protocolVersion, commandId, VirtualUartOpcode::kWriteShellChars, 1, byteCount};
}

/// 0x08: ReadShellChars (request up to maxBytes)
[[nodiscard]] inline VirtualUartEnvelope MakeReadShellCharsCommand(
    uint32_t protocolVersion, uint16_t commandId, uint32_t maxBytes) noexcept {
    return VirtualUartEnvelope{protocolVersion, commandId, VirtualUartOpcode::kReadShellChars, 1, maxBytes};
}

/// Parsed response envelope from 0xFFFF_C802_9000.
struct VirtualUartResponseEnvelope final {
    uint32_t protocolVersion{0};
    uint16_t commandId{0};
    uint8_t opcode{0};
    uint8_t operandSize{0};
    uint32_t operand{0}; // For 0x08 ReadShellChars, operand = available/returned byte count

    [[nodiscard]] static std::optional<VirtualUartResponseEnvelope> Decode(std::span<const uint8_t> raw) noexcept {
        if (raw.size() < kCommandEnvelopeBytes) {
            return std::nullopt;
        }
        VirtualUartResponseEnvelope env{};
        env.protocolVersion = LoadLittleEndianQuadlet(raw, 0);
        const uint32_t q1 = LoadLittleEndianQuadlet(raw, 4);
        env.commandId = static_cast<uint16_t>(q1 & 0xFFFF);
        env.opcode = static_cast<uint8_t>((q1 >> 16U) & 0xFF);
        env.operandSize = static_cast<uint8_t>((q1 >> 24U) & 0xFF);
        env.operand = LoadLittleEndianQuadlet(raw, 8);
        return env;
    }
};

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
