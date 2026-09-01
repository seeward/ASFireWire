#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "TopologyTypes.hpp"
#include "../Controller/ControllerTypes.hpp"

namespace ASFW::Driver {

class BusResetCoordinator;

class BusManager {
public:
    /**
     * @brief Optional PHY configuration fields to apply immediately before a bus reset.
     */
    struct PhyConfigCommand {
        std::optional<uint8_t> gapCount;
        std::optional<uint8_t> forceRootNodeID;
        std::optional<bool> setContender;
    };

    /**
     * @brief Reason why the bus manager decided to retool gap count.
     *
     * `MismatchForce63` mirrors Apple's early `processSelfIDs()` correction:
     * validated packet-0 gaps disagree, so the conservative corrective target is
     * `gap_count = 63`. The remaining reasons implement the later
     * `finishedBusScan()` stabilization rule.
     */
    enum class GapDecisionReason : uint8_t {
        MismatchForce63 = 0,
        ForcedGap = 1,
        TargetGap = 2,
        ZeroObservedGap = 3,
    };

    /**
     * @brief Typed outcome for gap-count optimization.
     */
    struct GapDecision {
        uint8_t gapCount{0x3F};
        GapDecisionReason reason{GapDecisionReason::MismatchForce63};
    };

    /**
     * @brief Gap-reset state tracked across generations.
     *
     * `lastConfirmedGap` is the most recent stable packet-0 gap observed on an
     * accepted topology. It remains `0xFF` until the first stable topology is
     * accepted so the optimizer can distinguish "unknown previous gap" from a
     * real confirmed `gap_count = 63`. `inFlight` is populated only after the
     * coordinator has successfully dispatched a corrective reset carrying a gap
     * update.
     */
    struct GapState {
        struct InFlightReset {
            uint8_t gapCount{0x3F};
            GapDecisionReason reason{GapDecisionReason::MismatchForce63};
        };

        uint8_t lastConfirmedGap{0xFF};
        std::optional<InFlightReset> inFlight;
    };

    enum class RootPolicy : uint8_t {
        Auto = 0,
        ForceLocal = 1,
        ForceNode = 2,
        Delegate = 3
    };

    struct Config {
        RootPolicy rootPolicy = RootPolicy::Auto;
        uint8_t forcedRootNodeID = 0xFF;
        bool delegateCycleMaster = false;
        bool enableGapOptimization = true;
        uint8_t forcedGapCount = 0;
        bool forcedGapFlag = false;
    };

    BusManager() = default;
    ~BusManager() = default;

    void SetRootPolicy(RootPolicy policy);
    void SetForcedRootNode(uint8_t nodeID);
    void SetDelegateMode(bool enable);
    void SetGapOptimizationEnabled(bool enable);
    void SetForcedGapCount(uint8_t gapCount);

    const Config& GetConfig() const { return config_; }
    [[nodiscard]] static const char* GapDecisionReasonString(GapDecisionReason reason) noexcept;

    /**
     * @brief Per-generation bus-scan evidence consumed by @ref AssignCycleMaster.
     *
     * Mirrors the three facts Apple's IOFireWireController carries out of a bus
     * scan into AssignCycleMaster()/finishedBusScan(): the per-node scan record
     * (`fScans[i]`), its IRM verdict (`fIRMisBad`), and whether any remote node
     * advertised BMC (`fBusMgr`).
     */
    struct BusScanEvidence {
        /// Indexed by physical ID: node failed empirical IRM read/lock
        /// verification. Apple: `fScans[i]->fIRMisBad`
        /// (IOFireWireController.cpp:2691, :2785).
        std::vector<bool> badIRMFlags;

        /// Indexed by physical ID: node's Config ROM was read this generation.
        /// Apple requires a scan record before a node may be handed root
        /// (IOFireWireController.cpp:2372 `if( fScans[i] )`). Leave empty when
        /// no scan evidence exists, and no scan filtering is applied.
        std::vector<bool> scanned;

        /// Apple `fBusMgr`: a remote node advertised BMC in its bus info block
        /// (IOFireWireController.cpp:2972-2974). Not proof of BUS_MANAGER_ID
        /// ownership — it only means a better candidate than us exists.
        bool remoteBusManagerCapable{false};
    };

    [[nodiscard]] std::optional<PhyConfigCommand> AssignCycleMaster(
        const TopologySnapshot& topology,
        const BusScanEvidence& evidence);

    /**
     * @brief Are the validated packet-0 Self-ID gap counts inconsistent?
     *
     * Unlike @ref EvaluateGapPolicy this carries no authority gate, because
     * Apple's mismatch detection in processSelfIDs()
     * (IOFireWireController.cpp:2139-2151) runs on every node before
     * bus-manager or IRM policy is resolved. The corrective action a non-IRM
     * node may take from it is limited to Apple's own: broadcast a PHY config
     * packet carrying gap 0x3F, without a bus reset.
     */
    [[nodiscard]] static bool HasGapCountMismatch(const std::vector<uint32_t>& selfIDs);

    /**
     * @brief Decide whether the current validated topology needs a gap retool reset.
     *
     * This implements a two-phase Apple-style policy:
     * - inconsistent packet-0 gaps force an early corrective `gap_count = 63`;
     * - otherwise, stable-bus optimization uses the current target gap together
     *   with the previous programmed gap to avoid unnecessary reset churn.
     */
    [[nodiscard]] std::optional<GapDecision> EvaluateGapPolicy(
        const TopologySnapshot& topology,
        const std::vector<uint32_t>& selfIDs);

    /**
     * @brief Record that a corrective reset carrying `gapCount` was actually dispatched.
     *
     * This is called only after PHY configuration transmission and reset
     * initiation both succeeded.
     */
    void NoteGapResetIssued(uint8_t gapCount, GapDecisionReason reason);

    /**
     * @brief Commit the stable packet-0 gap observed on an accepted topology.
     *
     * Callers must only invoke this when packet-0 gaps are consistent for the
     * accepted generation.
     */
    void NoteStableGapObserved(uint8_t observedGap);

    /**
     * @brief Drop any in-flight corrective target after a dispatch failure.
     */
    void ClearInFlightGapReset();

private:
    Config config_;
    GapState gapState_{};
};

} // namespace ASFW::Driver
