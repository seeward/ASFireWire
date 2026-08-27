// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "SPro24DspSignalTables.hpp"

namespace ASFW::Audio::DICE::Focusrite {

namespace {

using Kind = AudioSemanticSignalKind;

// All 41 entries of Pro24DSP_IpSigTab, in vendor order. Analog input numbering
// is NOT router channel order: Ins0:2 is "Anlg In 1" and the rear pair is 3/4.
// Confirmed against this unit's live router on 2026-08-27.
constexpr std::array<SPro24Signal, 41> kInputSignals = {{
    {Kind::AnalogLine, 1, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // Anlg In 1
    {Kind::AnalogLine, 2, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // Anlg In 2
    {Kind::AnalogLine, 3, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // Anlg In 3
    {Kind::AnalogLine, 4, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // Anlg In 4
    {Kind::DigitalSpdif, 1, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // SPDIF 1
    {Kind::DigitalSpdif, 2, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // SPDIF 2
    {Kind::DigitalAdat, 1, false, {kSPro24BlockAdat, kSPro24BlockAdat, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // ADAT In 1
    {Kind::DigitalAdat, 2, false, {kSPro24BlockAdat, kSPro24BlockAdat, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // ADAT In 2
    {Kind::DigitalAdat, 3, false, {kSPro24BlockAdat, kSPro24BlockAdat, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // ADAT In 3
    {Kind::DigitalAdat, 4, false, {kSPro24BlockAdat, kSPro24BlockAdat, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // ADAT In 4
    {Kind::DigitalAdat, 5, false, {kSPro24BlockAdat, kSPro24SignalAbsent, kSPro24SignalAbsent}, {4, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ADAT In 5 (1x only)
    {Kind::DigitalAdat, 6, false, {kSPro24BlockAdat, kSPro24SignalAbsent, kSPro24SignalAbsent}, {5, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ADAT In 6 (1x only)
    {Kind::DigitalAdat, 7, false, {kSPro24BlockAdat, kSPro24SignalAbsent, kSPro24SignalAbsent}, {6, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ADAT In 7 (1x only)
    {Kind::DigitalAdat, 8, false, {kSPro24BlockAdat, kSPro24SignalAbsent, kSPro24SignalAbsent}, {7, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ADAT In 8 (1x only)
    {Kind::HostStream, 1, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // DAW 1
    {Kind::HostStream, 2, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // DAW 2
    {Kind::HostStream, 3, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // DAW 3
    {Kind::HostStream, 4, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // DAW 4
    {Kind::HostStream, 5, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {4, 4, kSPro24SignalAbsent}},  // DAW 5
    {Kind::HostStream, 6, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {5, 5, kSPro24SignalAbsent}},  // DAW 6
    {Kind::HostStream, 7, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // DAW 7
    {Kind::HostStream, 8, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // DAW 8
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 0, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // FromMix1
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 1, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // FromMix2
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 2, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // FromMix3
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 3, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // FromMix4
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 4, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {4, 4, kSPro24SignalAbsent}},  // FromMix5
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 5, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {5, 5, kSPro24SignalAbsent}},  // FromMix6
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 6, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // FromMix7
    {Kind::Auxiliary, kSPro24AuxSourceMixReturnFirst + 7, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // FromMix8
    {Kind::Auxiliary, kSPro24AuxSourceReverbSendFirst + 0, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {8, 8, kSPro24SignalAbsent}},  // RvbSend-1
    {Kind::Auxiliary, kSPro24AuxSourceReverbSendFirst + 1, false, {kSPro24BlockMixer, kSPro24BlockMixer, kSPro24SignalAbsent}, {9, 9, kSPro24SignalAbsent}},  // RvbSend-2
    {Kind::DigitalSpdif, 3, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {4, 4, kSPro24SignalAbsent}},  // SPDIF 3
    {Kind::DigitalSpdif, 4, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {5, 5, kSPro24SignalAbsent}},  // SPDIF 4
    {Kind::Auxiliary, kSPro24AuxSourceEffectReturnFirst + 0, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {8, 4, kSPro24SignalAbsent}},  // FX(Anlg 1)
    {Kind::Auxiliary, kSPro24AuxSourceEffectReturnFirst + 1, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {9, 5, kSPro24SignalAbsent}},  // FX(Anlg 2)
    {Kind::Auxiliary, kSPro24AuxSourceReverbReturnFirst + 0, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {14, 6, kSPro24SignalAbsent}},  // FmRvb 0
    {Kind::Auxiliary, kSPro24AuxSourceReverbReturnFirst + 1, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {15, 7, kSPro24SignalAbsent}},  // FmRvb 1
    {Kind::Auxiliary, kSPro24AuxSourceArmFirst + 0, false, {kSPro24BlockArmApr, kSPro24BlockArmApr, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // FromArm-0
    {Kind::Auxiliary, kSPro24AuxSourceArmFirst + 1, false, {kSPro24BlockArmApr, kSPro24BlockArmApr, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // FromArm-1
    {Kind::Auxiliary, kSPro24AuxSourceMuted, false, {kSPro24BlockMute, kSPro24BlockMute, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // Off
}};

// All 47 entries of Pro24DSP_OpSigTab, in vendor order.
constexpr std::array<SPro24Signal, 47> kOutputSignals = {{
    {Kind::HostStream, 1, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // ToHost1
    {Kind::HostStream, 2, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // ToHost2
    {Kind::HostStream, 3, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // ToHost3
    {Kind::HostStream, 4, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // ToHost4
    {Kind::HostStream, 5, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {4, 4, kSPro24SignalAbsent}},  // ToHost5
    {Kind::HostStream, 6, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {5, 5, kSPro24SignalAbsent}},  // ToHost6
    {Kind::HostStream, 7, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // ToHost7
    {Kind::HostStream, 8, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // ToHost8
    {Kind::HostStream, 9, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {8, 8, kSPro24SignalAbsent}},  // ToHost9
    {Kind::HostStream, 10, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {9, 9, kSPro24SignalAbsent}},  // ToHost10
    {Kind::HostStream, 11, true, {kSPro24BlockAvs0, kSPro24SignalAbsent, kSPro24SignalAbsent}, {10, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ToHost11 (1x only)
    {Kind::HostStream, 12, true, {kSPro24BlockAvs0, kSPro24SignalAbsent, kSPro24SignalAbsent}, {11, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ToHost12 (1x only)
    {Kind::HostStream, 13, true, {kSPro24BlockAvs0, kSPro24SignalAbsent, kSPro24SignalAbsent}, {12, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ToHost13 (1x only)
    {Kind::HostStream, 14, true, {kSPro24BlockAvs0, kSPro24SignalAbsent, kSPro24SignalAbsent}, {13, kSPro24SignalAbsent, kSPro24SignalAbsent}},  // ToHost14 (1x only)
    {Kind::AnalogLine, 1, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // Mon. 1
    {Kind::AnalogLine, 2, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // Mon. 2
    {Kind::AnalogLine, 3, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // Line 3
    {Kind::AnalogLine, 4, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // Line 4
    {Kind::AnalogLine, 5, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {4, 8, kSPro24SignalAbsent}},  // Line 5  -- swaps with ToFX 0 at 2x
    {Kind::AnalogLine, 6, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {5, 9, kSPro24SignalAbsent}},  // Line 6  -- swaps with ToFX 1 at 2x
    {Kind::DigitalSpdif, 1, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // SPDIF 1.1
    {Kind::DigitalSpdif, 2, true, {kSPro24BlockAes, kSPro24BlockAes, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // SPDIF 1.2
    {Kind::Auxiliary, kSPro24AuxDestinationLoopbackFirst + 0, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {14, 10, kSPro24SignalAbsent}},  // Loop. 1
    {Kind::Auxiliary, kSPro24AuxDestinationLoopbackFirst + 1, true, {kSPro24BlockAvs0, kSPro24BlockAvs0, kSPro24SignalAbsent}, {15, 11, kSPro24SignalAbsent}},  // Loop. 2
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 0, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // ToMix1
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 1, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // ToMix2
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 2, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {2, 2, kSPro24SignalAbsent}},  // ToMix3
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 3, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {3, 3, kSPro24SignalAbsent}},  // ToMix4
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 4, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {4, 4, kSPro24SignalAbsent}},  // ToMix5
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 5, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {5, 5, kSPro24SignalAbsent}},  // ToMix6
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 6, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {6, 6, kSPro24SignalAbsent}},  // ToMix7
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 7, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {7, 7, kSPro24SignalAbsent}},  // ToMix8
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 8, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {8, 8, kSPro24SignalAbsent}},  // ToMix9
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 9, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {9, 9, kSPro24SignalAbsent}},  // ToMix10
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 10, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {10, 10, kSPro24SignalAbsent}},  // ToMix11
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 11, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {11, 11, kSPro24SignalAbsent}},  // ToMix12
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 12, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {12, 12, kSPro24SignalAbsent}},  // ToMix13
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 13, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {13, 13, kSPro24SignalAbsent}},  // ToMix14
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 14, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {14, 14, kSPro24SignalAbsent}},  // ToMix15
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 15, false, {kSPro24BlockMixerTx0, kSPro24BlockMixerTx0, kSPro24SignalAbsent}, {15, 15, kSPro24SignalAbsent}},  // ToMix16
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 16, false, {kSPro24BlockMixerTx1, kSPro24BlockMixerTx1, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // ToMix17
    {Kind::Auxiliary, kSPro24AuxDestinationMixerInputFirst + 17, false, {kSPro24BlockMixerTx1, kSPro24BlockMixerTx1, kSPro24SignalAbsent}, {1, 1, kSPro24SignalAbsent}},  // ToMix18
    {Kind::Auxiliary, kSPro24AuxDestinationEffectSendFirst + 0, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {8, 4, kSPro24SignalAbsent}},  // ToFX 0  -- swaps with Line 5 at 2x
    {Kind::Auxiliary, kSPro24AuxDestinationEffectSendFirst + 1, false, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {9, 5, kSPro24SignalAbsent}},  // ToFX 1  -- swaps with Line 6 at 2x
    {Kind::Auxiliary, kSPro24AuxDestinationReverbSendFirst + 0, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {14, 6, kSPro24SignalAbsent}},  // ToRvb 0
    {Kind::Auxiliary, kSPro24AuxDestinationReverbSendFirst + 1, true, {kSPro24BlockIns0, kSPro24BlockIns0, kSPro24SignalAbsent}, {15, 7, kSPro24SignalAbsent}},  // ToRvb 1
    {Kind::Auxiliary, kSPro24AuxDestinationOff, false, {kSPro24BlockOffDestination, kSPro24BlockOffDestination, kSPro24SignalAbsent}, {0, 0, kSPro24SignalAbsent}},  // Off -- reaches nowhere
}};

} // namespace

std::span<const SPro24Signal> SPro24InputSignals() noexcept { return kInputSignals; }
std::span<const SPro24Signal> SPro24OutputSignals() noexcept { return kOutputSignals; }

bool SPro24SignalCoordinates(const SPro24Signal& signal, DiceRateMode rateMode,
                             uint8_t& outBlock, uint8_t& outChannel) noexcept {
    const auto rate = static_cast<size_t>(rateMode);
    if (rate >= kSPro24RateModeCount) return false;
    if (signal.block[rate] == kSPro24SignalAbsent) return false;
    outBlock = signal.block[rate];
    outChannel = signal.channel[rate];
    return true;
}

namespace {

[[nodiscard]] const SPro24Signal* Find(std::span<const SPro24Signal> table,
                                       uint8_t block, uint8_t channel,
                                       DiceRateMode rateMode) noexcept {
    const auto rate = static_cast<size_t>(rateMode);
    if (rate >= kSPro24RateModeCount) return nullptr;
    for (const auto& signal : table) {
        if (signal.block[rate] == block && signal.channel[rate] == channel) return &signal;
    }
    return nullptr;
}

} // namespace

const SPro24Signal* FindSPro24InputSignal(uint8_t sourceBlock, uint8_t sourceChannel,
                                          DiceRateMode rateMode) noexcept {
    return Find(kInputSignals, sourceBlock, sourceChannel, rateMode);
}

const SPro24Signal* FindSPro24OutputSignal(uint8_t destinationBlock,
                                           uint8_t destinationChannel,
                                           DiceRateMode rateMode) noexcept {
    return Find(kOutputSignals, destinationBlock, destinationChannel, rateMode);
}

} // namespace ASFW::Audio::DICE::Focusrite
