// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "BeBoBVirtualUartClient.hpp"

#include "../../../../Discovery/DiscoveryTypes.hpp"
#include "../../../../Logging/Logging.hpp"

#include <algorithm>
#include <utility>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

namespace {

constexpr ASFW::FW::FwSpeed kVirtualUartSpeed = ASFW::FW::FwSpeed::S100;

} // namespace

BeBoBVirtualUartClient::BeBoBVirtualUartClient(
    ASFW::Async::IFireWireBusOps& busOps,
    Discovery::DeviceRouteToken route,
    uint32_t protocolVersion) noexcept
    : busOps_(busOps), route_(route), protocolVersion_(protocolVersion) {}

std::optional<ASFW::FW::NodeId>
BeBoBVirtualUartClient::OperationalNode() const noexcept {
    const auto node = Discovery::TryOperationalNodeId(route_.nodeId);
    if (!node) {
        return std::nullopt;
    }
    return ASFW::FW::NodeId{*node};
}

ASFW::Async::FWAddress BeBoBVirtualUartClient::AddressFor(
    uint32_t addressLo) const noexcept {
    return ASFW::Async::FWAddress{ASFW::Async::FWAddress::QualifiedAddressParts{
        .addressHi = kVirtualUartAddressHi,
        .addressLo = addressLo,
        .nodeID = route_.nodeId}};
}

void BeBoBVirtualUartClient::Cancel() noexcept {
    cancelled_ = true;
}

void BeBoBVirtualUartClient::SwitchToShell(std::function<void(bool success)> completion) {
    if (cancelled_ || !completion) {
        return;
    }
    const auto node = OperationalNode();
    if (!node) {
        completion(false);
        return;
    }

    const auto envelope = MakeSwitchToShellCommand(protocolVersion_, nextCommandId_++);
    const auto bytes = envelope.Bytes();

    ASFW_LOG(Firmware, "[VirtualUart] switch to 1394 shell node=%u gen=%u",
             static_cast<unsigned>(route_.nodeId),
             static_cast<unsigned>(route_.generation.value));

    (void)busOps_.WriteBlock(
        route_.generation, *node,
        AddressFor(kRequestAddressLo), bytes, kVirtualUartSpeed,
        [self = shared_from_this(), completion = std::move(completion)](
            ASFW::Async::AsyncStatus status,
            std::span<const uint8_t> /*payload*/) mutable {
            if (self->cancelled_) {
                return;
            }
            if (status != ASFW::Async::AsyncStatus::kSuccess) {
                ASFW_LOG(Firmware, "[VirtualUart] switch to shell write failed status=%{public}s",
                         ASFW::Async::ToString(status));
                completion(false);
                return;
            }
            completion(true);
        });
}

void BeBoBVirtualUartClient::WriteChars(
    std::string_view text, std::function<void(bool success)> completion) {
    if (cancelled_ || !completion || text.empty()) {
        if (completion) completion(false);
        return;
    }
    const auto node = OperationalNode();
    if (!node) {
        completion(false);
        return;
    }

    std::span<const uint8_t> payload{
        reinterpret_cast<const uint8_t*>(text.data()), text.size()};

    // Step 1: Write text to Request Data Buffer (0xFFFF_C802_1040)
    (void)busOps_.WriteBlock(
        route_.generation, *node,
        AddressFor(kRequestBufferAddressLo), payload, kVirtualUartSpeed,
        [self = shared_from_this(), textLength = static_cast<uint32_t>(text.size()),
         completion = std::move(completion), node](
            ASFW::Async::AsyncStatus status,
            std::span<const uint8_t> /*payload*/) mutable {
            if (self->cancelled_ || status != ASFW::Async::AsyncStatus::kSuccess) {
                completion(false);
                return;
            }

            // Step 2: Commit WriteShellChars envelope (Opcode 0x09) to AddrRegReq
            const auto envelope = MakeWriteShellCharsCommand(
                self->protocolVersion_, self->nextCommandId_++, textLength);
            const auto envBytes = envelope.Bytes();

            (void)self->busOps_.WriteBlock(
                self->route_.generation, *node,
                self->AddressFor(kRequestAddressLo), envBytes, kVirtualUartSpeed,
                [completion = std::move(completion)](
                    ASFW::Async::AsyncStatus envStatus,
                    std::span<const uint8_t> /*payload*/) {
                    completion(envStatus == ASFW::Async::AsyncStatus::kSuccess);
                });
        });
}

void BeBoBVirtualUartClient::ReadChars(
    uint32_t maxBytes, std::function<void(std::string output)> completion) {
    if (cancelled_ || !completion) {
        return;
    }
    const auto node = OperationalNode();
    if (!node) {
        completion("");
        return;
    }

    // Step 1: Send ReadShellChars request envelope (Opcode 0x08)
    const auto envelope = MakeReadShellCharsCommand(
        protocolVersion_, nextCommandId_++, maxBytes);
    const auto envBytes = envelope.Bytes();

    (void)busOps_.WriteBlock(
        route_.generation, *node,
        AddressFor(kRequestAddressLo), envBytes, kVirtualUartSpeed,
        [self = shared_from_this(), completion = std::move(completion), node](
            ASFW::Async::AsyncStatus status,
            std::span<const uint8_t> /*payload*/) mutable {
            if (self->cancelled_ || status != ASFW::Async::AsyncStatus::kSuccess) {
                completion("");
                return;
            }

            // Step 2: Read 12-byte response envelope from AddrRegResp (0xFFFF_C802_9000)
            (void)self->busOps_.ReadBlock(
                self->route_.generation, *node,
                self->AddressFor(kResponseAddressLo), kCommandEnvelopeBytes, kVirtualUartSpeed,
                [self, completion = std::move(completion), node](
                    ASFW::Async::AsyncStatus respStatus,
                    std::span<const uint8_t> respPayload) mutable {
                    if (self->cancelled_ || respStatus != ASFW::Async::AsyncStatus::kSuccess) {
                        completion("");
                        return;
                    }
                    const auto resp = VirtualUartResponseEnvelope::Decode(respPayload);
                    if (!resp || resp->operand == 0) {
                        completion("");
                        return;
                    }

                    const uint32_t bytesToRead = std::min(resp->operand, 1024U);

                    // Step 3: Read actual characters from AddrRegRespBuf (0xFFFF_C802_9040)
                    (void)self->busOps_.ReadBlock(
                        self->route_.generation, *node,
                        self->AddressFor(kResponseBufferAddressLo), bytesToRead, kVirtualUartSpeed,
                        [completion = std::move(completion)](
                            ASFW::Async::AsyncStatus dataStatus,
                            std::span<const uint8_t> dataPayload) {
                            if (dataStatus != ASFW::Async::AsyncStatus::kSuccess || dataPayload.empty()) {
                                completion("");
                                return;
                            }
                            std::string outStr(
                                reinterpret_cast<const char*>(dataPayload.data()),
                                dataPayload.size());
                            completion(std::move(outStr));
                        });
                });
        });
}

void BeBoBVirtualUartClient::ExecuteCommand(
    std::string_view command, std::function<void(std::string output)> completion) {
    if (cancelled_ || !completion) {
        return;
    }

    std::string cmdWithNewline{command};
    if (cmdWithNewline.empty() || cmdWithNewline.back() != '\n') {
        cmdWithNewline += "\r\n";
    }

    WriteChars(cmdWithNewline, [self = shared_from_this(), completion = std::move(completion)](bool ok) mutable {
        if (!ok) {
            completion("");
            return;
        }
        self->ReadChars(1024, std::move(completion));
    });
}

void BeBoBVirtualUartClient::ReadStreamingStats(
    std::function<void(std::optional<BeBoBStreamingStats>)> completion) {
    if (!completion) return;
    ExecuteCommand("sys stat", [completion = std::move(completion)](std::string stdoutText) {
        completion(BeBoBStreamTelemetryParser::ParseStreamingStats(stdoutText));
    });
}

void BeBoBVirtualUartClient::ReadAvStat(
    std::function<void(std::optional<BeBoBAvStat>)> completion) {
    if (!completion) return;
    ExecuteCommand("sys avstat all", [completion = std::move(completion)](std::string stdoutText) {
        completion(BeBoBStreamTelemetryParser::ParseAvStat(stdoutText));
    });
}

void BeBoBVirtualUartClient::ReadSyncState(
    std::function<void(std::optional<BeBoBSyncState>)> completion) {
    if (!completion) return;
    ExecuteCommand("fw sync show", [completion = std::move(completion)](std::string stdoutText) {
        completion(BeBoBStreamTelemetryParser::ParseSyncState(stdoutText));
    });
}

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
