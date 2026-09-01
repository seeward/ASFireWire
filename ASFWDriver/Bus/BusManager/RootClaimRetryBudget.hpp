// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 ASFW Project
//
// RootClaimRetryBudget.hpp — bounded retry budget for root-forcing bus policy.

#pragma once

#include <cstdint>

namespace ASFW::Bus {

/**
 * @brief Bounds how often bus policy may force a new root on one topology.
 *
 * Every root claim we make produces a bus reset and therefore a new generation,
 * which re-runs the same decision against the same physical bus. Without a
 * budget, a device that re-asserts root-hold-off after each reset is chased
 * forever. Apple bounds its equivalent path only with `fBusResetScheduled`
 * (IOFireWireController.cpp:3262), which does not survive across generations;
 * Linux bounds it explicitly with `card->bm_retries++ < 5` (core-card.c:493),
 * reset on topology change rather than on generation change. We follow Linux.
 *
 * The caller supplies a key from @ref ASFW::Driver::StableTopologyKey, which
 * deliberately excludes the root node so that forcing a root does not refill
 * the budget it just spent.
 */
class RootClaimRetryBudget {
public:
    static constexpr uint32_t kDefaultMaxAttempts = 5;

    explicit RootClaimRetryBudget(uint32_t maxAttempts = kDefaultMaxAttempts) noexcept
        : maxAttempts_(maxAttempts) {}

    /**
     * @brief Charge one attempt against @p topologyKey.
     * @return true if the caller may act; false once the budget is spent.
     *
     * A key different from the last one starts a fresh budget.
     */
    [[nodiscard]] bool TryConsume(uint32_t topologyKey) noexcept {
        if (topologyKey != topologyKey_) {
            topologyKey_ = topologyKey;
            attempts_ = 0;
        }

        if (attempts_ >= maxAttempts_) {
            return false;
        }

        ++attempts_;
        return true;
    }

    [[nodiscard]] uint32_t AttemptsOnCurrentTopology() const noexcept { return attempts_; }
    [[nodiscard]] uint32_t MaxAttempts() const noexcept { return maxAttempts_; }

    /// Forget the current topology so the next TryConsume starts a fresh budget.
    void Reset() noexcept {
        topologyKey_ = 0;
        attempts_ = 0;
    }

private:
    uint32_t maxAttempts_;
    uint32_t topologyKey_{0};
    uint32_t attempts_{0};
};

} // namespace ASFW::Bus
