// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// AudioSemanticMatrixStripStates.hpp -- mute/solo policy over a semantic matrix.
//
// Mute and solo are not hardware bits on a DICE-class mixer. A crosspoint
// coefficient of zero is what a mute *is*, so the level a strip returns to has
// to be remembered by the driver, and solo is "silence every other strip on
// this bus" expressed in the same coefficients. Both are therefore policy over
// remembered nominal levels, and both are device-independent: this file holds
// the policy, and a profile supplies only the native writes.

#pragma once

#include "IAudioSemanticMatrix.hpp"

namespace ASFW::Audio {

/// One writable strip resolved out of a published snapshot: the semantic axis
/// indices of the two destination rows and of the source cells that feed them.
/// A mono strip drives both destination rows from one source, so its two input
/// indices are equal; that is the only difference from a stereo strip here.
struct AudioSemanticMatrixStripCells final {
    uint32_t outputPresentationGroupId{0};
    uint32_t inputPresentationGroupId{0};
    uint32_t outputLeft{0};
    uint32_t outputRight{0};
    uint32_t inputLeft{0};
    uint32_t inputRight{0};

    [[nodiscard]] constexpr bool IsMono() const noexcept { return inputLeft == inputRight; }
};

inline constexpr uint32_t kMaxAudioSemanticMatrixStripsPerBus =
    kMaxAudioSemanticMatrixInputs;

/// Resolves the strip formed by one destination group and one source group, or
/// false when that pair is not a usable control. The published crosspoint
/// presentation is authoritative: a hidden or readback-only cell never becomes
/// a strip merely because both groups exist.
[[nodiscard]] bool ResolveAudioSemanticMatrixStrip(
    const AudioSemanticMatrixSnapshot& snapshot,
    uint32_t outputPresentationGroupId,
    uint32_t inputPresentationGroupId,
    AudioSemanticMatrixStripCells& outStrip) noexcept;

/// Every writable strip on one bus, in matrix input order. Used by solo, which
/// has to touch the bus's other strips rather than only the one gestured on.
[[nodiscard]] bool EnumerateAudioSemanticMatrixBusStrips(
    const AudioSemanticMatrixSnapshot& snapshot,
    uint32_t outputPresentationGroupId,
    std::array<AudioSemanticMatrixStripCells,
               kMaxAudioSemanticMatrixStripsPerBus>& outStrips,
    uint32_t& outCount) noexcept;

/// The sparse mute/solo record set a device keeps alongside its coefficients.
/// Records exist only for strips that need one; see AudioSemanticMatrixStripState.
class AudioSemanticMatrixStripStateSet final {
public:
    [[nodiscard]] uint32_t Count() const noexcept { return count_; }

    [[nodiscard]] const AudioSemanticMatrixStripState* Find(
        uint32_t outputPresentationGroupId,
        uint32_t inputPresentationGroupId) const noexcept;

    /// Inserts or replaces one record. Fails closed when the bound is reached
    /// rather than dropping a record, because a dropped record is a nominal
    /// level that can never be restored.
    [[nodiscard]] bool Upsert(const AudioSemanticMatrixStripState& state) noexcept;

    void Remove(uint32_t outputPresentationGroupId,
                uint32_t inputPresentationGroupId) noexcept;

    /// Drops records that no longer carry anything: not muted, not soloed, and
    /// on a bus with no solo, so the live coefficient is already the nominal.
    void Prune() noexcept;

    [[nodiscard]] bool BusHasSolo(uint32_t outputPresentationGroupId) const noexcept;

    /// True when the user muted this strip, or another strip on its bus is
    /// soloed and this one is not.
    [[nodiscard]] bool IsSuppressed(uint32_t outputPresentationGroupId,
                                    uint32_t inputPresentationGroupId) const noexcept;

    void CopyInto(AudioSemanticMatrixSnapshot& snapshot) const noexcept;

    void Clear() noexcept { count_ = 0; states_ = {}; }

private:
    uint32_t count_{0};
    std::array<AudioSemanticMatrixStripState,
               kMaxAudioSemanticMatrixStripStates> states_{};
};

/// The pair of coefficients one strip's cells should currently hold: its
/// nominal, or zero while it is suppressed.
struct AudioSemanticMatrixStripCoefficients final {
    uint16_t left{0};
    uint16_t right{0};
};

/// Folds a strip's remembered nominal and the bus's suppression state into the
/// values its cells must hold. A strip with no record takes its nominal from
/// the live coefficients, which is what "no record means nothing remembered"
/// has to mean for the very first mute of a strip to be reversible.
[[nodiscard]] AudioSemanticMatrixStripCoefficients
AudioSemanticMatrixStripEffective(const AudioSemanticMatrixSnapshot& snapshot,
                                  const AudioSemanticMatrixStripStateSet& states,
                                  const AudioSemanticMatrixStripCells& strip) noexcept;

/// The nominal a strip currently carries, ignoring suppression. Used when a
/// level gesture arrives for a muted strip: the fader must move without
/// unmuting, so the new level is remembered rather than written.
[[nodiscard]] AudioSemanticMatrixStripCoefficients
AudioSemanticMatrixStripNominal(const AudioSemanticMatrixSnapshot& snapshot,
                                const AudioSemanticMatrixStripStateSet& states,
                                const AudioSemanticMatrixStripCells& strip) noexcept;

} // namespace ASFW::Audio
