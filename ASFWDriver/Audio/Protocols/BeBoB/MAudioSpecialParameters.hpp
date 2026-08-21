// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialParameters.hpp — cached image for the FireWire 1814's
// write-only 0x00..0x9c parameter window.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ASFW::Audio::BeBoB {

// These IDs are semantic app/driver contracts, not register offsets. Their
// mapping is local to this family, keeping the user client unable to issue an
// arbitrary write into the M-Audio parameter window.
enum class MAudio1814ControlId : uint32_t {
    AnalogOutput12Level = 1,
    AnalogOutput34Level = 2,
    Headphone12Level = 3,
    Headphone34Level = 4,
    AnalogOutput12Source = 5,
    AnalogOutput34Source = 6,
    Headphone12Source = 7,
    Headphone34Source = 8,
    PhysicalMixerSendMask = 9,
    StreamMixerSendMask = 10,
};

enum class MAudio1814OutputSource : int32_t { Mixer = 0, Aux = 1 };
enum class MAudio1814HeadphoneSource : int32_t {
    Mixer12 = 0,
    Mixer34 = 1,
    Aux = 2,
};

class MAudioSpecialParameterImage final {
public:
    static constexpr size_t kQuadletCount = 40;
    static constexpr int32_t kLevelMin = -32768;
    static constexpr int32_t kLevelMax = 0;
    static constexpr uint32_t kPhysicalMixerSendMask = 0x0003'FFFFU;
    static constexpr uint32_t kStreamMixerSendMask = 0x0000'000FU;

    MAudioSpecialParameterImage() noexcept {
        values_[kBalanceFirst] = kBalanceHardPanned;
        for (size_t i = kBalanceFirst + 1; i <= kBalanceLast; ++i) {
            values_[i] = kBalanceHardPanned;
        }
        values_[kMixerStreamSourceIndex] = 0x0000'0009U;
        values_[kHeadphonePairSourceIndex] = 0x0002'0001U;
    }

    [[nodiscard]] const std::array<uint32_t, kQuadletCount>& Values() const noexcept {
        return values_;
    }

    [[nodiscard]] uint32_t ValueAt(size_t index) const noexcept {
        return index < values_.size() ? values_[index] : 0;
    }

    [[nodiscard]] bool Apply(MAudio1814ControlId control, int32_t value,
                             size_t& outChangedIndex) noexcept {
        switch (control) {
        case MAudio1814ControlId::AnalogOutput12Level:
            return SetPairedLevel(kAnalogOutput12LevelIndex, value, outChangedIndex);
        case MAudio1814ControlId::AnalogOutput34Level:
            return SetPairedLevel(kAnalogOutput34LevelIndex, value, outChangedIndex);
        case MAudio1814ControlId::Headphone12Level:
            return SetPairedLevel(kHeadphone12LevelIndex, value, outChangedIndex);
        case MAudio1814ControlId::Headphone34Level:
            return SetPairedLevel(kHeadphone34LevelIndex, value, outChangedIndex);
        case MAudio1814ControlId::AnalogOutput12Source:
            return SetFlag(kAnalogOutPairSourceIndex, 0U, value, 1U, outChangedIndex);
        case MAudio1814ControlId::AnalogOutput34Source:
            return SetFlag(kAnalogOutPairSourceIndex, 1U, value, 1U, outChangedIndex);
        case MAudio1814ControlId::Headphone12Source:
            return SetHeadphoneSource(0U, value, outChangedIndex);
        case MAudio1814ControlId::Headphone34Source:
            return SetHeadphoneSource(16U, value, outChangedIndex);
        case MAudio1814ControlId::PhysicalMixerSendMask:
            return SetMask(kMixerPhysSourceIndex, value, kPhysicalMixerSendMask, outChangedIndex);
        case MAudio1814ControlId::StreamMixerSendMask:
            return SetMask(kMixerStreamSourceIndex, value, kStreamMixerSendMask, outChangedIndex);
        }
        return false;
    }

    [[nodiscard]] int32_t ControlValue(MAudio1814ControlId control) const noexcept {
        switch (control) {
        case MAudio1814ControlId::AnalogOutput12Level:
            return PairedLevel(kAnalogOutput12LevelIndex);
        case MAudio1814ControlId::AnalogOutput34Level:
            return PairedLevel(kAnalogOutput34LevelIndex);
        case MAudio1814ControlId::Headphone12Level:
            return PairedLevel(kHeadphone12LevelIndex);
        case MAudio1814ControlId::Headphone34Level:
            return PairedLevel(kHeadphone34LevelIndex);
        case MAudio1814ControlId::AnalogOutput12Source:
            return static_cast<int32_t>(values_[kAnalogOutPairSourceIndex] & 0x01U);
        case MAudio1814ControlId::AnalogOutput34Source:
            return static_cast<int32_t>((values_[kAnalogOutPairSourceIndex] >> 1U) & 0x01U);
        case MAudio1814ControlId::Headphone12Source:
            return DecodeHeadphone(values_[kHeadphonePairSourceIndex] & 0x07U);
        case MAudio1814ControlId::Headphone34Source:
            return DecodeHeadphone((values_[kHeadphonePairSourceIndex] >> 16U) & 0x07U);
        case MAudio1814ControlId::PhysicalMixerSendMask:
            return static_cast<int32_t>(values_[kMixerPhysSourceIndex] & kPhysicalMixerSendMask);
        case MAudio1814ControlId::StreamMixerSendMask:
            return static_cast<int32_t>(values_[kMixerStreamSourceIndex] & kStreamMixerSendMask);
        }
        return 0;
    }

private:
    static constexpr size_t kAnalogOutput12LevelIndex = 2;  // +0x08
    static constexpr size_t kAnalogOutput34LevelIndex = 3;  // +0x0c
    static constexpr size_t kHeadphone12LevelIndex = 14;    // +0x38
    static constexpr size_t kHeadphone34LevelIndex = 15;    // +0x3c
    static constexpr size_t kBalanceFirst = 16;              // +0x40
    static constexpr size_t kBalanceLast = 24;               // +0x60
    static constexpr uint32_t kBalanceHardPanned = 0x7FFE8000U;
    static constexpr size_t kMixerPhysSourceIndex = 36;      // +0x90
    static constexpr size_t kMixerStreamSourceIndex = 37;    // +0x94
    static constexpr size_t kHeadphonePairSourceIndex = 38;  // +0x98
    static constexpr size_t kAnalogOutPairSourceIndex = 39;  // +0x9c

    [[nodiscard]] bool SetPairedLevel(size_t index, int32_t value,
                                      size_t& outChangedIndex) noexcept {
        if (value < kLevelMin || value > kLevelMax) return false;
        const uint32_t sample = static_cast<uint16_t>(static_cast<int16_t>(value));
        values_[index] = (sample << 16U) | sample;
        outChangedIndex = index;
        return true;
    }

    [[nodiscard]] bool SetFlag(size_t index, uint32_t shift, int32_t value,
                               uint32_t maxValue, size_t& outChangedIndex) noexcept {
        if (value < 0 || static_cast<uint32_t>(value) > maxValue) return false;
        const uint32_t mask = maxValue << shift;
        values_[index] = (values_[index] & ~mask) | (static_cast<uint32_t>(value) << shift);
        outChangedIndex = index;
        return true;
    }

    [[nodiscard]] bool SetHeadphoneSource(uint32_t shift, int32_t value,
                                           size_t& outChangedIndex) noexcept {
        if (value < static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer12) ||
            value > static_cast<int32_t>(MAudio1814HeadphoneSource::Aux)) {
            return false;
        }
        constexpr uint32_t mask = 0x07U;
        values_[kHeadphonePairSourceIndex] =
            (values_[kHeadphonePairSourceIndex] & ~(mask << shift)) |
            (1U << (static_cast<uint32_t>(value) + shift));
        outChangedIndex = kHeadphonePairSourceIndex;
        return true;
    }

    [[nodiscard]] bool SetMask(size_t index, int32_t value, uint32_t allowed,
                               size_t& outChangedIndex) noexcept {
        if (value < 0 || (static_cast<uint32_t>(value) & ~allowed) != 0U) return false;
        values_[index] = (values_[index] & ~allowed) | static_cast<uint32_t>(value);
        outChangedIndex = index;
        return true;
    }

    [[nodiscard]] int32_t PairedLevel(size_t index) const noexcept {
        return static_cast<int32_t>(static_cast<int16_t>(values_[index] >> 16U));
    }

    [[nodiscard]] static int32_t DecodeHeadphone(uint32_t encoded) noexcept {
        if (encoded & 0x04U) return static_cast<int32_t>(MAudio1814HeadphoneSource::Aux);
        if (encoded & 0x02U) return static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer34);
        return static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer12);
    }

    std::array<uint32_t, kQuadletCount> values_{};
};

} // namespace ASFW::Audio::BeBoB
