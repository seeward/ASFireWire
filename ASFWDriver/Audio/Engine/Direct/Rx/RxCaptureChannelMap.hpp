// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// RxCaptureChannelMap.hpp — how one device's AM824 capture slots land on
// CoreAudio channels.
//
// This is the neutral shape only: which slot feeds which channel, and how far
// a channel must be delayed to undo a capture skew inside the device. It
// carries no device identity — the tables live with the family that needs them
// (see Audio/Families/BeBoB/MAudio/MAudioCaptureChannelMap.hpp) and arrive here
// already chosen.
//
// A default-constructed map is the identity: slot N feeds channel N with no
// delay, which is what every device that reports an honest channel order gets.

#pragma once

#include "../../../Wire/AMDTP/AmdtpRateGeometry.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ASFW::AudioEngine::Direct::Rx {

struct RxCaptureChannelMap final {
    /// `slotForChannel[ch]` is the AM824 slot that feeds CoreAudio channel `ch`.
    /// slotCount == 0 means identity. Entries must be < the packet's data block size;
    /// the decoder drops the map rather than read out of bounds if they are not.
    std::array<uint8_t, ASFW::Encoding::kMaxPcmChannels> slotForChannel{};
    uint32_t slotCount{0};

    /// The capture width this table was built for. A device that changes its
    /// formation (the 1814 switches between a 10-channel S/PDIF and a
    /// 16-channel ADAT capture) must not have a table for one width applied to
    /// the other, so this is checked against the live geometry rather than
    /// assumed to still match.
    uint32_t channelCount{0};

    /// Frames of delay applied to the channels named by `delayedChannelMask`.
    /// Zero disables the delay path entirely.
    uint32_t delayFrames{0};

    /// Bit N set means CoreAudio channel N is delayed by `delayFrames`.
    uint32_t delayedChannelMask{0};

    template <size_t Count>
    [[nodiscard]] constexpr bool SetSlots(
        const std::array<uint8_t, Count>& slots) noexcept {
        static_assert(Count <= ASFW::Encoding::kMaxPcmChannels);
        slotCount = static_cast<uint32_t>(Count);
        for (size_t index = 0; index < Count; ++index) {
            slotForChannel[index] = slots[index];
        }
        return true;
    }

    [[nodiscard]] bool SetSlots(std::span<const uint8_t> slots) noexcept {
        if (slots.size() > slotForChannel.size()) return false;
        slotCount = static_cast<uint32_t>(slots.size());
        for (size_t index = 0; index < slots.size(); ++index) {
            slotForChannel[index] = slots[index];
        }
        return true;
    }

    [[nodiscard]] constexpr bool IsIdentity() const noexcept {
        return slotCount == 0 && delayFrames == 0;
    }

    [[nodiscard]] constexpr bool HasDelay() const noexcept {
        return delayFrames != 0 && delayedChannelMask != 0;
    }

    /// True when every entry addresses a slot the packet actually carries.
    /// Checked per packet against the live DBS, because the profile's geometry
    /// and the device's DBS are independent facts and a mismatch here would
    /// otherwise read past the data blocks.
    [[nodiscard]] constexpr bool FitsWithin(uint32_t channels,
                                            uint32_t dataBlockSize) const noexcept {
        if (slotCount == 0) {
            return true;
        }
        if (channelCount != channels || slotCount < channels) {
            return false;
        }
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (slotForChannel[ch] >= dataBlockSize) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr uint32_t SlotFor(uint32_t channel) const noexcept {
        return slotCount == 0 ? channel : slotForChannel[channel];
    }

    [[nodiscard]] constexpr bool IsDelayed(uint32_t channel) const noexcept {
        return channel < 32U && ((delayedChannelMask >> channel) & 1U) != 0U;
    }
};

} // namespace ASFW::AudioEngine::Direct::Rx
