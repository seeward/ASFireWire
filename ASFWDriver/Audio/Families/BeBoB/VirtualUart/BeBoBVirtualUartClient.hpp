// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// BeBoBVirtualUartClient.hpp — Asynchronous 1394 Virtual UART client for BeBoB devices.

#pragma once

#include "BeBoBStreamTelemetryParser.hpp"
#include "BeBoBTelemetryTypes.hpp"
#include "BeBoBVirtualUartCommand.hpp"

#include "../../../../Async/Interfaces/IFireWireBusOps.hpp"
#include "../../../../Discovery/DeviceRouteToken.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ASFW::Audio::Families::BeBoB::VirtualUart {

class BeBoBVirtualUartClient final
    : public std::enable_shared_from_this<BeBoBVirtualUartClient> {
public:
    BeBoBVirtualUartClient(ASFW::Async::IFireWireBusOps& busOps,
                           Discovery::DeviceRouteToken route,
                           uint32_t protocolVersion = 1) noexcept;

    /// 0x07: Switch the device shell console to IEEE 1394 Virtual UART.
    void SwitchToShell(std::function<void(bool success)> completion);

    /// 0x09: Write a command or stdin string to the shell.
    void WriteChars(std::string_view text, std::function<void(bool success)> completion);

    /// 0x08: Read available stdout characters from the shell.
    void ReadChars(uint32_t maxBytes, std::function<void(std::string output)> completion);

    /// High-level: Writes command string, waits for processing, and reads the output buffer.
    void ExecuteCommand(std::string_view command, std::function<void(std::string output)> completion);

    /// Runs `sys stat` and parses the streaming telemetry into a structured model.
    void ReadStreamingStats(std::function<void(std::optional<BeBoBStreamingStats>)> completion);

    /// Runs `sys avstat` and parses the silicon lock flags.
    void ReadAvStat(std::function<void(std::optional<BeBoBAvStat>)> completion);

    /// Runs `fw sync show` and parses the master clock sync state.
    void ReadSyncState(std::function<void(std::optional<BeBoBSyncState>)> completion);

    void Cancel() noexcept;

    [[nodiscard]] const Discovery::DeviceRouteToken& Route() const noexcept {
        return route_;
    }

private:
    [[nodiscard]] ASFW::Async::FWAddress AddressFor(uint32_t addressLo) const noexcept;
    [[nodiscard]] std::optional<ASFW::FW::NodeId> OperationalNode() const noexcept;

    ASFW::Async::IFireWireBusOps& busOps_;
    Discovery::DeviceRouteToken route_{};
    uint32_t protocolVersion_{1};
    uint16_t nextCommandId_{1};
    bool cancelled_{false};
};

} // namespace ASFW::Audio::Families::BeBoB::VirtualUart
