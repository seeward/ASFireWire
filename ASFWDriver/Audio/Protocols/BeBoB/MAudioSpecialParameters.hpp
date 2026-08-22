// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MAudioSpecialParameters.hpp — cached image of the FireWire 1814's write-only
// 0x00..0x9f parameter window at 0xffc700700000.
//
// The window is 160 bytes and the device answers no read for any of it, so this
// image is the only account of the device's mixer state that exists. Every
// quadlet must be asserted at bring-up; nothing may be left to power-on state.
//
// The image is held in **wire order (big-endian)**, which is how both reference
// implementations hold it, and for the same reason: every field is a plain
// big-endian i16 or a big-endian bitmask, so the one byte swap belongs at field
// access rather than smeared over a serializer.
//
//   references/alsa-userspace-control-protocols-impl/protocols/bebob/src/maudio/special.rs
//     — MaudioSpecialStateCache, the eighteen OFFSET_RANGEs, and the serdes that
//       prove every byte belongs to exactly one range.
//   vendor kext, decoded in docs/MAUDIO_1814_KEXT_RE.md §6
//     — CustomMixerWriteQuads (the address), SendLevelsToDevice (__ROL2__ 8, i.e.
//       the image is big-endian), SetInputRouting / SetHeadphoneSelector /
//       SetAuxMonitor (the bit layouts), SetInputPan (the value scale).
//
// The two references agree on every offset and every bit position. They disagree
// on exactly one thing — balance polarity — and the vendor wins there; see
// kBalanceHardPannedFirst.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ASFW::Audio::BeBoB {

/// A family of like-typed controls occupying one contiguous range of the window.
///
/// These IDs are a semantic app/driver contract, not register offsets. The
/// mapping to offsets is local to this header, which is what keeps the user
/// client unable to issue an arbitrary write into a vendor register window.
enum class MAudio1814ControlGroup : uint32_t {
    MixerStreamGain = 0x01,       ///< playback 1-4 into the main mixer
    AnalogOutputVolume = 0x02,    ///< analog out 1-4
    MixerAnalogGain = 0x03,       ///< analog in 1-8 into the main mixer
    MixerSpdifGain = 0x04,        ///< S/PDIF in 1-2 into the main mixer
    MixerAdatGain = 0x05,         ///< ADAT in 1-8 into the main mixer
    AuxOutputVolume = 0x06,       ///< aux bus out 1-2
    HeadphoneVolume = 0x07,       ///< headphone 1-4
    MixerAnalogBalance = 0x08,    ///< analog in 1-8 pan into the main mixer
    MixerSpdifBalance = 0x09,     ///< S/PDIF in 1-2 pan
    MixerAdatBalance = 0x0A,      ///< ADAT in 1-8 pan
    AuxStreamGain = 0x0B,         ///< playback 1-4 into the aux bus
    AuxAnalogGain = 0x0C,         ///< analog in 1-8 into the aux bus
    AuxSpdifGain = 0x0D,          ///< S/PDIF in 1-2 into the aux bus
    AuxAdatGain = 0x0E,           ///< ADAT in 1-8 into the aux bus
    PhysicalMixerSendMask = 0x0F, ///< which physical pairs feed which mixer pair
    StreamMixerSendMask = 0x10,   ///< which stream pairs feed which mixer pair
    HeadphoneSource = 0x11,       ///< source of headphone pair 1-2
    AnalogOutputSource = 0x12,    ///< source of analog output pair 1-2
};

/// Controls are addressed as `(group << 8) | index`, index zero-based within the
/// group. One flat `uint32_t` keeps the user-client wire format a plain id/value
/// pair while still naming 78 distinct controls.
[[nodiscard]] constexpr uint32_t MakeMAudio1814ControlId(MAudio1814ControlGroup group,
                                                         uint32_t index) noexcept {
    return (static_cast<uint32_t>(group) << 8U) | (index & 0xFFU);
}

[[nodiscard]] constexpr MAudio1814ControlGroup MAudio1814ControlGroupOf(uint32_t id) noexcept {
    return static_cast<MAudio1814ControlGroup>(id >> 8U);
}

[[nodiscard]] constexpr uint32_t MAudio1814ControlIndexOf(uint32_t id) noexcept {
    return id & 0xFFU;
}

/// What a control writes, and where.
enum class MAudio1814ControlKind : uint8_t {
    Level,    ///< i16, kLevelMin..kLevelMax — 0 is unity
    Balance,  ///< i16, full signed range — 0 is centre
    Mask,     ///< bitmask within one quadlet
    Selector, ///< small enum packed at a per-index shift
};

struct MAudio1814ControlGroupInfo final {
    MAudio1814ControlGroup group{};
    MAudio1814ControlKind kind{};
    uint16_t byteOffset{};  ///< start of the group's range in the window
    uint8_t count{};        ///< number of controls in the group
    const char* name{nullptr};
};

/// The eighteen ranges of the window, in offset order. Together they tile
/// 0x00..0xa0 exactly once — `MAudioSpecialParametersTests` asserts that, the
/// same invariant the crate's `offset_ranges` test asserts.
inline constexpr std::array<MAudio1814ControlGroupInfo, 18> kMAudio1814ControlGroups{{
    {MAudio1814ControlGroup::MixerStreamGain, MAudio1814ControlKind::Level, 0x00, 4,
     "mixer stream gain"},
    {MAudio1814ControlGroup::AnalogOutputVolume, MAudio1814ControlKind::Level, 0x08, 4,
     "analog output volume"},
    {MAudio1814ControlGroup::MixerAnalogGain, MAudio1814ControlKind::Level, 0x10, 8,
     "mixer analog gain"},
    {MAudio1814ControlGroup::MixerSpdifGain, MAudio1814ControlKind::Level, 0x20, 2,
     "mixer S/PDIF gain"},
    {MAudio1814ControlGroup::MixerAdatGain, MAudio1814ControlKind::Level, 0x24, 8,
     "mixer ADAT gain"},
    {MAudio1814ControlGroup::AuxOutputVolume, MAudio1814ControlKind::Level, 0x34, 2,
     "aux output volume"},
    {MAudio1814ControlGroup::HeadphoneVolume, MAudio1814ControlKind::Level, 0x38, 4,
     "headphone volume"},
    {MAudio1814ControlGroup::MixerAnalogBalance, MAudio1814ControlKind::Balance, 0x40, 8,
     "mixer analog balance"},
    {MAudio1814ControlGroup::MixerSpdifBalance, MAudio1814ControlKind::Balance, 0x50, 2,
     "mixer S/PDIF balance"},
    {MAudio1814ControlGroup::MixerAdatBalance, MAudio1814ControlKind::Balance, 0x54, 8,
     "mixer ADAT balance"},
    {MAudio1814ControlGroup::AuxStreamGain, MAudio1814ControlKind::Level, 0x64, 4,
     "aux stream gain"},
    {MAudio1814ControlGroup::AuxAnalogGain, MAudio1814ControlKind::Level, 0x6C, 8,
     "aux analog gain"},
    {MAudio1814ControlGroup::AuxSpdifGain, MAudio1814ControlKind::Level, 0x7C, 2,
     "aux S/PDIF gain"},
    {MAudio1814ControlGroup::AuxAdatGain, MAudio1814ControlKind::Level, 0x80, 8,
     "aux ADAT gain"},
    {MAudio1814ControlGroup::PhysicalMixerSendMask, MAudio1814ControlKind::Mask, 0x90, 1,
     "physical mixer send mask"},
    {MAudio1814ControlGroup::StreamMixerSendMask, MAudio1814ControlKind::Mask, 0x94, 1,
     "stream mixer send mask"},
    {MAudio1814ControlGroup::HeadphoneSource, MAudio1814ControlKind::Selector, 0x98, 2,
     "headphone source"},
    {MAudio1814ControlGroup::AnalogOutputSource, MAudio1814ControlKind::Selector, 0x9C, 2,
     "analog output source"},
}};

/// Total number of addressable controls across all groups.
inline constexpr size_t kMAudio1814ControlCount = [] {
    size_t total = 0;
    for (const auto& info : kMAudio1814ControlGroups) total += info.count;
    return total;
}();

[[nodiscard]] constexpr const MAudio1814ControlGroupInfo* MAudio1814GroupInfo(
    MAudio1814ControlGroup group) noexcept {
    for (const auto& info : kMAudio1814ControlGroups) {
        if (info.group == group) return &info;
    }
    return nullptr;
}

/// Source of one analog output pair. Wire encoding — never renumber.
/// `SetAuxMonitor` sets the bit to mean "take aux instead of the mixer pair".
enum class MAudio1814OutputSource : int32_t { Mixer = 0, Aux = 1 };

/// Source of one headphone pair. Wire encoding — never renumber.
enum class MAudio1814HeadphoneSource : int32_t { Mixer12 = 0, Mixer34 = 1, Aux = 2 };

class MAudioSpecialParameterImage final {
public:
    static constexpr size_t kImageBytes = 160;
    static constexpr size_t kQuadletCount = kImageBytes / sizeof(uint32_t);

    /// Level fields run from fully attenuated to unity. `SetAllLevels` in the
    /// vendor kext resets every level to 0, and the crate names the same two
    /// endpoints GAIN_MIN / GAIN_MAX.
    static constexpr int32_t kLevelMin = -32768;
    static constexpr int32_t kLevelMax = 0;

    /// Balance fields are centred at zero and use the full signed range.
    static constexpr int32_t kBalanceMin = -32768;
    static constexpr int32_t kBalanceMax = 32767;

    /// The vendor's factory balance default, from `FWSettingsLevels::
    /// ResetToFactorySettings`: pan -255 on the first channel of each pair and
    /// +255 on the second, scaled by the -128 multiplier that `SetInputPan`
    /// encodes. That gives ±32640, not ±32768 — the domain is ±255 steps of 128.
    ///
    /// The ALSA crate defaults to the *opposite* polarity ([MIN, MAX]). Only one
    /// of the two can have the L/R sense right and the vendor is authoritative
    /// for this hardware, so we follow the kext. Nothing observable distinguishes
    /// them until a physical input is routed into the main mixer.
    static constexpr int32_t kBalanceHardPannedFirst = 32640;
    static constexpr int32_t kBalanceHardPannedSecond = -32640;

    static constexpr uint32_t kPhysicalMixerSendMask = 0x0003'FFFFU;
    static constexpr uint32_t kStreamMixerSendMask = 0x0000'000FU;

    MAudioSpecialParameterImage() noexcept { ResetToDefaults(); }

    /// The power-on state the device should be put into.
    ///
    /// A zero-filled window is **not** a safe default: zero means unity on every
    /// level field, which would leave all eighteen physical inputs summing into
    /// the aux bus — and therefore into the headphones — at full gain. The aux
    /// defaults below come from the crate's `MaudioSpecialAuxParameters::default`
    /// and mute everything except analog 1/2.
    void ResetToDefaults() noexcept {
        bytes_.fill(0);

        for (const auto& info : kMAudio1814ControlGroups) {
            if (info.kind != MAudio1814ControlKind::Balance) continue;
            for (uint8_t i = 0; i < info.count; ++i) {
                SetField(info.byteOffset + i * sizeof(int16_t),
                         static_cast<int16_t>((i % 2 == 0) ? kBalanceHardPannedFirst
                                                           : kBalanceHardPannedSecond));
            }
        }

        // Aux bus: analog 1/2 open at unity, every other physical source muted.
        MuteRange(MAudio1814ControlGroup::AuxAnalogGain, 2);
        MuteRange(MAudio1814ControlGroup::AuxSpdifGain, 0);
        MuteRange(MAudio1814ControlGroup::AuxAdatGain, 0);

        // Stream 1/2 -> mixer pair 1, stream 3/4 -> mixer pair 2. This is the
        // routing that makes playback audible at all; see the 1814 bring-up notes.
        SetQuadletAt(0x94 / 4, 0x0000'0009U);
        // Headphone pair 1 follows mixer pair 1, pair 2 follows mixer pair 2.
        SetQuadletAt(0x98 / 4, 0x0002'0001U);
        // Analog output pairs take their mixer pair, not the aux bus.
        SetQuadletAt(0x9C / 4, 0x0000'0000U);
    }

    /// The window exactly as it goes on the wire.
    [[nodiscard]] std::span<const uint8_t> Bytes() const noexcept { return bytes_; }

    /// One quadlet in host order — for tests, logging, and the single-quadlet
    /// write path. Out-of-range reads yield zero rather than trapping.
    [[nodiscard]] uint32_t QuadletAt(size_t index) const noexcept {
        if (index >= kQuadletCount) return 0;
        const size_t offset = index * sizeof(uint32_t);
        return (static_cast<uint32_t>(bytes_[offset]) << 24U) |
               (static_cast<uint32_t>(bytes_[offset + 1]) << 16U) |
               (static_cast<uint32_t>(bytes_[offset + 2]) << 8U) |
               static_cast<uint32_t>(bytes_[offset + 3]);
    }

    /// Replaces one whole quadlet with an already validated wire value. This is
    /// deliberately narrower than exposing the byte image: the protocol uses it
    /// only to advance its confirmed write-only belief after the matching async
    /// write has acknowledged.
    [[nodiscard]] bool SetQuadlet(size_t index, uint32_t value) noexcept {
        if (index >= kQuadletCount) return false;
        SetQuadletAt(index, value);
        return true;
    }

    /// Applies one semantic control. On success `outChangedIndex` names the
    /// single quadlet that changed, which is the unit the device is written in —
    /// every range in this window is quadlet-aligned and every level field is
    /// half of one, so a stereo pair is exactly one transaction.
    [[nodiscard]] bool Apply(uint32_t controlId, int32_t value,
                             size_t& outChangedIndex) noexcept {
        const auto* info = MAudio1814GroupInfo(MAudio1814ControlGroupOf(controlId));
        if (!info) return false;
        const uint32_t index = MAudio1814ControlIndexOf(controlId);
        if (index >= info->count) return false;

        switch (info->kind) {
        case MAudio1814ControlKind::Level:
            if (value < kLevelMin || value > kLevelMax) return false;
            return SetScalar(*info, index, static_cast<int16_t>(value), outChangedIndex);
        case MAudio1814ControlKind::Balance:
            if (value < kBalanceMin || value > kBalanceMax) return false;
            return SetScalar(*info, index, static_cast<int16_t>(value), outChangedIndex);
        case MAudio1814ControlKind::Mask:
            return SetMask(*info, value, outChangedIndex);
        case MAudio1814ControlKind::Selector:
            return SetSelector(*info, index, value, outChangedIndex);
        }
        return false;
    }

    /// Current driver-side belief for one control. Returns zero for an unknown
    /// id, which is indistinguishable from a legitimately zero control — callers
    /// enumerate ids from `kMAudio1814ControlGroups` rather than guessing.
    [[nodiscard]] int32_t ControlValue(uint32_t controlId) const noexcept {
        const auto* info = MAudio1814GroupInfo(MAudio1814ControlGroupOf(controlId));
        if (!info) return 0;
        const uint32_t index = MAudio1814ControlIndexOf(controlId);
        if (index >= info->count) return 0;

        switch (info->kind) {
        case MAudio1814ControlKind::Level:
        case MAudio1814ControlKind::Balance:
            return Field(info->byteOffset + index * sizeof(int16_t));
        case MAudio1814ControlKind::Mask:
            return static_cast<int32_t>(QuadletAt(info->byteOffset / sizeof(uint32_t)) &
                                        MaskFor(*info));
        case MAudio1814ControlKind::Selector:
            return DecodeSelector(*info, index);
        }
        return 0;
    }

private:
    [[nodiscard]] static constexpr uint32_t MaskFor(
        const MAudio1814ControlGroupInfo& info) noexcept {
        return info.group == MAudio1814ControlGroup::PhysicalMixerSendMask
                   ? kPhysicalMixerSendMask
                   : kStreamMixerSendMask;
    }

    /// Selector packing. `SetHeadphoneSelector` clears a three-bit field and sets
    /// a one-hot flag within it; the analog output selector is a single bit per
    /// pair with 0 = mixer, 1 = aux.
    [[nodiscard]] static constexpr uint32_t SelectorShift(
        const MAudio1814ControlGroupInfo& info, uint32_t index) noexcept {
        return info.group == MAudio1814ControlGroup::HeadphoneSource ? index * 16U : index;
    }

    [[nodiscard]] static constexpr uint32_t SelectorFieldMask(
        const MAudio1814ControlGroupInfo& info) noexcept {
        return info.group == MAudio1814ControlGroup::HeadphoneSource ? 0x07U : 0x01U;
    }

    void MuteRange(MAudio1814ControlGroup group, uint8_t openCount) noexcept {
        const auto* info = MAudio1814GroupInfo(group);
        if (!info) return;
        for (uint8_t i = openCount; i < info->count; ++i) {
            SetField(info->byteOffset + i * sizeof(int16_t), static_cast<int16_t>(kLevelMin));
        }
    }

    [[nodiscard]] bool SetScalar(const MAudio1814ControlGroupInfo& info, uint32_t index,
                                 int16_t value, size_t& outChangedIndex) noexcept {
        const size_t offset = info.byteOffset + index * sizeof(int16_t);
        SetField(offset, value);
        outChangedIndex = offset / sizeof(uint32_t);
        return true;
    }

    [[nodiscard]] bool SetMask(const MAudio1814ControlGroupInfo& info, int32_t value,
                               size_t& outChangedIndex) noexcept {
        const uint32_t allowed = MaskFor(info);
        if (value < 0 || (static_cast<uint32_t>(value) & ~allowed) != 0U) return false;
        const size_t quadlet = info.byteOffset / sizeof(uint32_t);
        SetQuadletAt(quadlet, (QuadletAt(quadlet) & ~allowed) | static_cast<uint32_t>(value));
        outChangedIndex = quadlet;
        return true;
    }

    [[nodiscard]] bool SetSelector(const MAudio1814ControlGroupInfo& info, uint32_t index,
                                   int32_t value, size_t& outChangedIndex) noexcept {
        const uint32_t field = SelectorFieldMask(info);
        const uint32_t shift = SelectorShift(info, index);
        uint32_t encoded = 0;
        if (info.group == MAudio1814ControlGroup::HeadphoneSource) {
            if (value < static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer12) ||
                value > static_cast<int32_t>(MAudio1814HeadphoneSource::Aux)) {
                return false;
            }
            encoded = 1U << static_cast<uint32_t>(value); // one-hot within the field
        } else {
            if (value < static_cast<int32_t>(MAudio1814OutputSource::Mixer) ||
                value > static_cast<int32_t>(MAudio1814OutputSource::Aux)) {
                return false;
            }
            encoded = static_cast<uint32_t>(value);
        }
        const size_t quadlet = info.byteOffset / sizeof(uint32_t);
        SetQuadletAt(quadlet,
                     (QuadletAt(quadlet) & ~(field << shift)) | (encoded << shift));
        outChangedIndex = quadlet;
        return true;
    }

    [[nodiscard]] int32_t DecodeSelector(const MAudio1814ControlGroupInfo& info,
                                         uint32_t index) const noexcept {
        const uint32_t raw = (QuadletAt(info.byteOffset / sizeof(uint32_t)) >>
                              SelectorShift(info, index)) &
                             SelectorFieldMask(info);
        if (info.group != MAudio1814ControlGroup::HeadphoneSource) {
            return static_cast<int32_t>(raw);
        }
        if (raw & 0x04U) return static_cast<int32_t>(MAudio1814HeadphoneSource::Aux);
        if (raw & 0x02U) return static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer34);
        return static_cast<int32_t>(MAudio1814HeadphoneSource::Mixer12);
    }

    [[nodiscard]] int16_t Field(size_t byteOffset) const noexcept {
        return static_cast<int16_t>((static_cast<uint16_t>(bytes_[byteOffset]) << 8U) |
                                    static_cast<uint16_t>(bytes_[byteOffset + 1]));
    }

    void SetField(size_t byteOffset, int16_t value) noexcept {
        const auto raw = static_cast<uint16_t>(value);
        bytes_[byteOffset] = static_cast<uint8_t>(raw >> 8U);
        bytes_[byteOffset + 1] = static_cast<uint8_t>(raw);
    }

    void SetQuadletAt(size_t index, uint32_t value) noexcept {
        const size_t offset = index * sizeof(uint32_t);
        bytes_[offset] = static_cast<uint8_t>(value >> 24U);
        bytes_[offset + 1] = static_cast<uint8_t>(value >> 16U);
        bytes_[offset + 2] = static_cast<uint8_t>(value >> 8U);
        bytes_[offset + 3] = static_cast<uint8_t>(value);
    }

    std::array<uint8_t, kImageBytes> bytes_{};
};

} // namespace ASFW::Audio::BeBoB
