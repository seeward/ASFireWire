// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 ASFW Project

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace ASFW::Driver {

/**
 * @brief Gap count selection from bus geometry.
 *
 * Indexed by hop count, from P1394a draft 4 table C-2. Both references carry
 * this same table: Apple as `gaps[26]` (IOFireWireController.cpp:3209-3212) and
 * Linux as `gap_count_table[]` (core-card.c:276-278, truncated at 16 hops with
 * everything beyond falling back to 63).
 *
 * @note Apple appears to add a second term, `newGap = max(pingGap, hopGap)`,
 *       where `pingGap` derives from `fFWIM->getPingTimes()`
 *       (IOFireWireController.cpp:3290-3318). That term is **inert on shipping
 *       Apple hardware**, established by reversing AppleFWOHCI 559:
 *         - `IOFireWireLink::getPingTimes()`, the base implementation, returns
 *           NULL (IOFireWireLink.cpp:149-152), and the controller dereferences
 *           the result with no null check — so the FWIM override exists mainly
 *           to hand back valid storage.
 *         - `AppleFWOHCI::getPingTimes()` (AppleFWOHCI559 __text:0x861C) is a
 *           three-instruction `lea rax, [rdi+0BC0h]; ret`. The array is 64
 *           inline UInt32 at +0xBC0..0xCC0; the next member sits at +0xCC4.
 *         - That `lea` is the ONLY reference to any offset in 0xBC0..0xCBF in
 *           the entire __text segment, and the function's only xref is its
 *           vtable slot (0x20C58) — no in-kext caller. Nothing ever writes a
 *           ping time, and IOKit zeroes instance memory.
 *       With every entry zero, `maxPing` is 0, so `maxPing >= 29` is false and
 *       `pingGap` is always 5. Five is the table minimum for every hop count
 *       >= 1, so `max(pingGap, hopGap) == hopGap` always. Hop-only selection is
 *       therefore Apple's *effective* behaviour, not a reduction of it — and it
 *       is also exactly what Linux ships (core-card.c:482-485).
 *
 * @note One real difference remains, and it is in the hop count rather than the
 *       table. Apple passes `maxHops = fRootNodeID` — its comment: "Do lazy gap
 *       count optimization. Assume the bus is a daisy-chain (worst case) so hop
 *       count == root ID" (IOFireWireController.cpp:3296-3301). We pass the
 *       measured bus diameter, as Linux does with `root_node->max_hops`. On any
 *       branched topology ours is the smaller, tighter, and correct value;
 *       Apple's is deliberately conservative. `BusManager::Config::forcedGapCount`
 *       remains the override if a bus ever needs a larger gap than geometry says.
 */
class GapCountOptimizer {
public:
    static constexpr uint8_t GAP_TABLE[26] = {
        63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40,
        43, 46, 48, 51, 54, 57, 59, 62, 63, 63
    };

    static uint8_t CalculateFromHops(uint8_t maxHops);

    static bool ShouldUpdate(const std::vector<uint8_t>& currentGaps,
                            uint8_t newGap,
                            uint8_t prevGap = 0xFF);

    static bool AreGapsConsistent(const std::vector<uint8_t>& gaps);

    static bool HasInvalidGap(const std::vector<uint8_t>& gaps);
};

} // namespace ASFW::Driver
