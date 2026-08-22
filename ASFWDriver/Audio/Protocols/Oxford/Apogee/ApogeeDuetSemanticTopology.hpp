// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// ApogeeDuetSemanticTopology.hpp -- protocol-independent Duet signal model.

#pragma once

#include "../../../Shared/Topology/IAudioSemanticTopology.hpp"

namespace ASFW::Audio::Oxford::Apogee {

inline constexpr uint32_t kApogeeDuetSemanticDeviceKind = 0x4455'4554; // "DUET"

/// Builds the immutable graph of a Duet FireWire at the current supported
/// formation. The protocol adapter translates parameter and route IDs into
/// vendor FCP operations below this boundary.
[[nodiscard]] bool BuildApogeeDuetSemanticTopology(
    AudioSemanticTopologySnapshot& outSnapshot) noexcept;

} // namespace ASFW::Audio::Oxford::Apogee
