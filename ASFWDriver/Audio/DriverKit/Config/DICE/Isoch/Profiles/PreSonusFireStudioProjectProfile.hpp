// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../DiceDeviceProfile.hpp"

#include <array>

namespace ASFW::Isoch::Audio::DICE::Profiles {

// Experimental 44.1/48 kHz profile using geometry read from a real Project.
// Short playback/capture, rate switches and 44.1 kHz S/PDIF playback were
// verified on one unit; latency and sustained stability remain unvalidated.
// See captures/presonus-firestudio-project/.
class PreSonusFireStudioProjectProfile final : public IDiceDeviceProfile {
public:
    static constexpr uint32_t kSampleRateHz = 48000;
    static constexpr std::array<uint32_t, 2> kSupportedSampleRatesHz{44100, 48000};
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
