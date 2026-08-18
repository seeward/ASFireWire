// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBVirtualUartClient.hpp — Asynchronous 1394 Virtual UART client for BeBoB devices.

#pragma once

#include "BeBoBVirtualUartCommand.hpp"

#include "../../../../Async/Interfaces/IFireWireBusOps.hpp"
#include "../../../../Discovery/DeviceRouteToken.hpp"
#include "../../../../Scheduling/ITimerScheduler.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

/// Owner of the DM1000 mailbox conversation.
///
/// The mailbox is half-duplex and single-occupancy: two overlapping
/// conversations are answered with rCode 4 (resp_conflict_error), and a drain
/// that loses its request/response pairing silently re-serves the same 128-byte
/// page forever. This client is therefore the *only* sanctioned path to the
/// mailbox — every caller (UI, MCP, driver-internal telemetry) queues here so
/// exactly one conversation is ever in flight. Serialising above this point
/// cannot work: an in-process lock is blind to other processes and to raw block
/// transactions aimed at the same address window.
class BeBoBVirtualUartClient final
    : public std::enable_shared_from_this<BeBoBVirtualUartClient> {
public:
    /// `timers` may be null in contexts with no scheduler, in which case the
    /// inter-page settles are skipped and the drain paces itself on bus
    /// round-trip latency alone.
    BeBoBVirtualUartClient(ASFW::Async::IFireWireBusOps& busOps,
                           Discovery::DeviceRouteToken route,
                           Scheduling::ITimerScheduler* timers = nullptr,
                           uint32_t protocolVersion = 1) noexcept;

    /// 0x07: Switch the device shell console to IEEE 1394 Virtual UART.
    void SwitchToShell(std::function<void(bool success)> completion);

    /// 0x09: Write a command or stdin string to the shell.
    void WriteChars(std::string_view text, std::function<void(bool success)> completion);

    /// 0x08: Read available stdout characters from the shell.
    void ReadChars(uint32_t maxBytes, std::function<void(std::string output)> completion);

    /// Drains stdout until the device reports the FIFO empty on a run of
    /// consecutive polls. A single empty poll means nothing: output arrives in
    /// bursts as the RTOS produces it.
    void DrainStdout(std::function<void(std::string output)> completion);

    /// High-level: writes the command, lets the shell run, and drains its
    /// output. Queued — safe to call while another conversation is in flight.
    void ExecuteCommand(std::string_view command, std::function<void(std::string output)> completion);

    /// Conversations queued behind the one currently in flight.
    [[nodiscard]] size_t QueueDepth() const noexcept { return pending_.size(); }

    /// True while a conversation owns the mailbox.
    [[nodiscard]] bool Busy() const noexcept { return busy_; }

    void Cancel() noexcept;

    [[nodiscard]] const Discovery::DeviceRouteToken& Route() const noexcept {
        return route_;
    }

    /// Consecutive empty polls that end a drain.
    static constexpr uint32_t kQuietPollsBeforeDone = 3;
    /// Upper bound on pages per drain, so a device emitting continuously (the
    /// unanchored PLL-calibration loop does exactly that) cannot wedge us.
    static constexpr uint32_t kMaxDrainPages = 48;
    /// Settle between pages, and after a command before its first poll.
    static constexpr uint64_t kPageSettleNs = 15ULL * 1000ULL * 1000ULL;
    static constexpr uint64_t kCommandSettleNs = 200ULL * 1000ULL * 1000ULL;

private:
    struct PendingCommand {
        std::string command;
        std::function<void(std::string)> completion;
    };

    [[nodiscard]] ASFW::Async::FWAddress AddressFor(uint32_t addressLo) const noexcept;
    [[nodiscard]] std::optional<ASFW::FW::NodeId> OperationalNode() const noexcept;

    /// Starts the next queued conversation when the mailbox is free.
    void PumpQueue();
    /// Runs one conversation to completion, then releases the mailbox.
    void RunCommand(PendingCommand request);
    void DrainPage(std::shared_ptr<std::string> accumulated,
                   uint32_t pagesRead,
                   uint32_t quietPolls,
                   std::function<void(std::string)> completion);
    void AfterDelay(uint64_t delayNs, std::function<void()> work);

    ASFW::Async::IFireWireBusOps& busOps_;
    Discovery::DeviceRouteToken route_{};
    Scheduling::ITimerScheduler* timers_{nullptr};
    uint32_t protocolVersion_{1};
    uint16_t nextCommandId_{1};
    bool cancelled_{false};
    bool busy_{false};
    std::vector<PendingCommand> pending_;
};

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
