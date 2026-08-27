// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// SPro24DspControls.hpp -- semantic controls published for Saffire Pro 24 DSP.
//
// These IDs name hardware behaviour, never application-section offsets or
// DICE fields. The UI may use them only through AudioControlSurfaceSnapshot.

#pragma once

#include <cstdint>

namespace ASFW::Audio::DICE::Focusrite::SPro24DspControl {

inline constexpr uint32_t kMicInputMode1 = 0x5350'0001;
inline constexpr uint32_t kMicInputMode2 = 0x5350'0002;
inline constexpr uint32_t kLineInputLevel34 = 0x5350'0003;
inline constexpr uint32_t kLineInputLevel56 = 0x5350'0004;

inline constexpr uint32_t kOutputVolumeFirst = 0x5350'0100; // 1/2, 3/4, 5/6 lanes.
inline constexpr uint32_t kOutputMuteFirst   = 0x5350'0110;
inline constexpr uint32_t kGlobalMute        = 0x5350'0120;
inline constexpr uint32_t kGlobalDim         = 0x5350'0121;

/// Read-only route source for each physical output pair. The value is a
/// profile-level source identity, never a TCAT block/channel packed into the
/// UI ABI. Route changes remain unavailable until their vendor commit path is
/// independently verified.
inline constexpr uint32_t kOutputRouteSourceFirst = 0x5350'0130;

enum class OutputRouteSource : int32_t {
    Unknown = 0,
    HostPlayback12 = 1,
    HostPlayback34 = 2,
    HostPlayback56 = 3,
    HostPlayback78 = 4,
    Mixer12 = 16,
    Mixer34 = 17,
    Mixer56 = 18,
    Mixer78 = 19,
    Analog12 = 32,
    Spdif12 = 48,
};

inline constexpr uint32_t kChannelStripEqFirst        = 0x5350'0200;
inline constexpr uint32_t kChannelStripCompressorFirst = 0x5350'0210;
inline constexpr uint32_t kChannelStripEqAfterCompFirst = 0x5350'0220;
inline constexpr uint32_t kReverbEnabled = 0x5350'0230;
inline constexpr uint32_t kInSituMode    = 0x5350'0231;

} // namespace ASFW::Audio::DICE::Focusrite::SPro24DspControl
