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

/// Block transactions against this mailbox must be a whole number of quadlets.
/// An unaligned request-buffer write drops its tail: "help\r\n" lands as "help"
/// with the CR/LF replaced by whatever the previous command left at those
/// offsets, so the device echoes a garbled line and never executes it.
constexpr size_t QuadletAligned(size_t length) noexcept {
    return (length + 3U) & ~static_cast<size_t>(3U);
}

} // namespace

BeBoBVirtualUartClient::BeBoBVirtualUartClient(
    ASFW::Async::IFireWireBusOps& busOps,
    Discovery::DeviceRouteToken route,
    Scheduling::ITimerScheduler* timers,
    uint32_t protocolVersion) noexcept
    : busOps_(busOps), route_(route), timers_(timers),
      protocolVersion_(protocolVersion) {}

void BeBoBVirtualUartClient::AfterDelay(uint64_t delayNs,
                                        std::function<void()> work) {
    if (!work) {
        return;
    }
    if (timers_ == nullptr) {
        work();
        return;
    }
    (void)timers_->ScheduleAfter(delayNs, std::move(work));
}

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

    // The envelope below carries the true, unpadded length, so padding the
    // buffer write is safe: the device consumes exactly `text.size()` bytes.
    auto padded = std::make_shared<std::vector<uint8_t>>(QuadletAligned(text.size()), 0U);
    std::copy(text.begin(), text.end(), padded->begin());
    std::span<const uint8_t> payload{padded->data(), padded->size()};

    // Step 1: Write text to Request Data Buffer (0xFFFF_C802_1040)
    (void)busOps_.WriteBlock(
        route_.generation, *node,
        AddressFor(kRequestBufferAddressLo), payload, kVirtualUartSpeed,
        [self = shared_from_this(), textLength = static_cast<uint32_t>(text.size()),
         padded, completion = std::move(completion), node](
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
                    // The stdout tail page is almost never quadlet-aligned, and
                    // an unaligned block read fails outright — which silently
                    // lost the end of every response. Read aligned, trim back.
                    const auto alignedRead =
                        static_cast<uint32_t>(QuadletAligned(bytesToRead));

                    // Step 3: Read actual characters from AddrRegRespBuf (0xFFFF_C802_9040)
                    (void)self->busOps_.ReadBlock(
                        self->route_.generation, *node,
                        self->AddressFor(kResponseBufferAddressLo), alignedRead, kVirtualUartSpeed,
                        [completion = std::move(completion), bytesToRead](
                            ASFW::Async::AsyncStatus dataStatus,
                            std::span<const uint8_t> dataPayload) {
                            if (dataStatus != ASFW::Async::AsyncStatus::kSuccess || dataPayload.empty()) {
                                completion("");
                                return;
                            }
                            const size_t usable =
                                std::min(static_cast<size_t>(bytesToRead), dataPayload.size());
                            std::string outStr(
                                reinterpret_cast<const char*>(dataPayload.data()), usable);
                            completion(std::move(outStr));
                        });
                });
        });
}

void BeBoBVirtualUartClient::DrainPage(std::shared_ptr<std::string> accumulated,
                                       uint32_t pagesRead,
                                       uint32_t quietPolls,
                                       std::function<void(std::string)> completion) {
    if (cancelled_) {
        completion(std::move(*accumulated));
        return;
    }
    if (pagesRead >= kMaxDrainPages || quietPolls >= kQuietPollsBeforeDone) {
        completion(std::move(*accumulated));
        return;
    }

    ReadChars(1024, [self = shared_from_this(), accumulated, pagesRead, quietPolls,
                     completion = std::move(completion)](std::string page) mutable {
        if (page.empty()) {
            // A single empty poll proves nothing — stdout arrives in bursts as
            // the RTOS produces it. Only a run of them ends the drain.
            self->AfterDelay(kPageSettleNs,
                             [self, accumulated, pagesRead, quietPolls,
                              completion = std::move(completion)]() mutable {
                                 self->DrainPage(accumulated, pagesRead,
                                                 quietPolls + 1, std::move(completion));
                             });
            return;
        }
        accumulated->append(page);
        self->AfterDelay(kPageSettleNs,
                         [self, accumulated, pagesRead, completion = std::move(completion)]() mutable {
                             self->DrainPage(accumulated, pagesRead + 1, 0, std::move(completion));
                         });
    });
}

void BeBoBVirtualUartClient::DrainStdout(std::function<void(std::string output)> completion) {
    if (!completion) {
        return;
    }
    DrainPage(std::make_shared<std::string>(), 0, 0, std::move(completion));
}

void BeBoBVirtualUartClient::ExecuteCommand(
    std::string_view command, std::function<void(std::string output)> completion) {
    if (cancelled_ || !completion) {
        return;
    }
    pending_.push_back(PendingCommand{std::string{command}, std::move(completion)});
    PumpQueue();
}

void BeBoBVirtualUartClient::PumpQueue() {
    if (busy_ || pending_.empty() || cancelled_) {
        return;
    }
    busy_ = true;
    PendingCommand next = std::move(pending_.front());
    pending_.erase(pending_.begin());
    RunCommand(std::move(next));
}

void BeBoBVirtualUartClient::RunCommand(PendingCommand request) {
    // The shell terminates lines on CRLF. A bare LF is echoed but never runs,
    // so normalise whatever the caller passed rather than appending blindly.
    std::string line = std::move(request.command);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    line += "\r\n";

    auto finish = [self = shared_from_this(),
                   completion = std::move(request.completion)](std::string output) mutable {
        completion(std::move(output));
        self->busy_ = false;
        self->PumpQueue();
    };

    // Discard anything already staged so this command's drain returns its own
    // output rather than the tail of the previous conversation.
    DrainStdout([self = shared_from_this(), line = std::move(line),
                 finish = std::move(finish)](std::string) mutable {
        self->WriteChars(line, [self, finish = std::move(finish)](bool ok) mutable {
            if (!ok) {
                finish("");
                return;
            }
            self->AfterDelay(kCommandSettleNs, [self, finish = std::move(finish)]() mutable {
                self->DrainStdout(std::move(finish));
            });
        });
    });
}

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
