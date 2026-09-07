// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../DiceDeviceProfile.hpp"

namespace ASFW::Isoch::Audio::DICE::Profiles {

// Experimental, 48 kHz-only profile using geometry read from a real Project.
// Short capture/playback and GarageBand use were verified on one unit; hardware
// latency and sustained stability remain unvalidated. See captures/presonus-firestudio-project/.
class PreSonusFireStudioProjectProfile final : public IDiceDeviceProfile {
public:
    static constexpr uint32_t kSampleRateHz = 48000;
    static constexpr uint16_t kPcmChannels = 10;
    static constexpr uint16_t kMidiPorts = 1;
    static constexpr uint8_t kMidiSlots = 1;
    static constexpr uint8_t kDbs = 11;
    static constexpr uint32_t kStreamCount = 1;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] bool Matches(const DiceDeviceIdentity& identity) const noexcept override;
    [[nodiscard]] DiceDeviceQuirks Quirks() const noexcept override;
    [[nodiscard]] std::vector<uint32_t> SupportedSampleRates() const override;
    [[nodiscard]] bool BuildDefaultTxStreamConfig(DiceStreamConfig& out) const noexcept override;
    [[nodiscard]] bool BuildDefaultRxStreamConfig(DiceStreamConfig& out) const noexcept override;
};

} // namespace ASFW::Isoch::Audio::DICE::Profiles
