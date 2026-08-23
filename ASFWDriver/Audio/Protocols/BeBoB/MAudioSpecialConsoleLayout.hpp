// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#pragma once

#include "MAudioSpecialFormation.hpp"
#include "../../Shared/Topology/IAudioSemanticConsoleLayout.hpp"

namespace ASFW::Audio::BeBoB {

inline constexpr uint32_t kMAudioSpecialSemanticDeviceKind = 0x4D41'3134; // "MA14"

/// Builds the presentation-neutral console geometry for the M-Audio special
/// firmware. The parameter-window control IDs are consumed only here; the app
/// receives them as opaque semantic bindings.
[[nodiscard]] bool BuildMAudioSpecialConsoleLayout(
    MAudioDigitalFormat captureFormat,
    AudioSemanticConsoleLayoutSnapshot& outSnapshot) noexcept;

} // namespace ASFW::Audio::BeBoB
