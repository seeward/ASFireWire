// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// SPro24DspSemanticMatrix.hpp -- Saffire Pro 24 DSP mixer projection.
//
// TCAT router block/channel fields are decoded below the semantic boundary.
// The resulting matrix axes identify only stable mixer ports and the current
// source signal class; the UI receives none of the vendor block IDs.

#pragma once

#include "../Core/DICEExtensionState.hpp"
#include "../../../Shared/Topology/IAudioSemanticMatrix.hpp"

namespace ASFW::Audio::DICE::Focusrite {

inline constexpr uint32_t kSPro24DspSemanticDeviceKind = 0x5350'3234; // "SP24"

[[nodiscard]] bool BuildSPro24DspSemanticMatrix(
    const DiceMixerCoefficients& coefficients,
    const DiceRouterEntries& routes,
    AudioSemanticMatrixSnapshot& outSnapshot) noexcept;

} // namespace ASFW::Audio::DICE::Focusrite
