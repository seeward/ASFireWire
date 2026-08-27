// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Protocols/DICE/Focusrite/SPro24DspSignalTables.hpp"

namespace ASFW::Audio::DICE::Focusrite {
namespace {

using Kind = AudioSemanticSignalKind;
constexpr auto kLow = DiceRateMode::Low;
constexpr auto kMid = DiceRateMode::Middle;
constexpr auto kHigh = DiceRateMode::High;

TEST(SPro24SignalTableTests, CarriesEveryVendorEntryInBothDirections) {
    EXPECT_EQ(SPro24InputSignals().size(), 41U);
    EXPECT_EQ(SPro24OutputSignals().size(), 47U);
}

TEST(SPro24SignalTableTests, NamesDestinationsAtTheLowRate) {
    const auto* monitorLeft = FindSPro24OutputSignal(kSPro24BlockIns0, 0, kLow);
    ASSERT_NE(monitorLeft, nullptr);
    EXPECT_EQ(monitorLeft->kind, Kind::AnalogLine);
    EXPECT_EQ(monitorLeft->signalIndex, 1U); // Mon. 1

    const auto* lineThree = FindSPro24OutputSignal(kSPro24BlockIns0, 2, kLow);
    ASSERT_NE(lineThree, nullptr);
    EXPECT_EQ(lineThree->signalIndex, 3U);

    const auto* spdif = FindSPro24OutputSignal(kSPro24BlockAes, 6, kLow);
    ASSERT_NE(spdif, nullptr);
    EXPECT_EQ(spdif->kind, Kind::DigitalSpdif);
    EXPECT_EQ(spdif->signalIndex, 1U);

    // ToMix1 is mixer input 1, reached through MixerTx0:0.
    const auto* toMixOne = FindSPro24OutputSignal(kSPro24BlockMixerTx0, 0, kLow);
    ASSERT_NE(toMixOne, nullptr);
    EXPECT_EQ(toMixOne->signalIndex, kSPro24AuxDestinationMixerInputFirst);
    // ToMix17/18 come through MixerTx1, continuing the same numbering.
    const auto* toMixSeventeen = FindSPro24OutputSignal(kSPro24BlockMixerTx1, 0, kLow);
    ASSERT_NE(toMixSeventeen, nullptr);
    EXPECT_EQ(toMixSeventeen->signalIndex, kSPro24AuxDestinationMixerInputFirst + 16U);

    // "Off" is a destination that reaches nowhere; the captured router uses it
    // twice so Mixer:0/1 occupy a peak slot.
    const auto* off = FindSPro24OutputSignal(kSPro24BlockOffDestination, 0, kLow);
    ASSERT_NE(off, nullptr);
    EXPECT_EQ(off->signalIndex, kSPro24AuxDestinationOff);
}

// The headline hazard. At 88.2/96 kHz Line 5/6 and ToFX 0/1 exchange router
// channels outright, so a decoder that hardcodes the 1x channels reports the
// channel-strip send as a line output and the line output as a send.
TEST(SPro24SignalTableTests, LineFiveSixAndEffectSendExchangePositionsAtMidRate) {
    // 1x: Line 5/6 at Ins0:4/5, ToFX 0/1 at Ins0:8/9.
    const auto* lineFiveLow = FindSPro24OutputSignal(kSPro24BlockIns0, 4, kLow);
    ASSERT_NE(lineFiveLow, nullptr);
    EXPECT_EQ(lineFiveLow->kind, Kind::AnalogLine);
    EXPECT_EQ(lineFiveLow->signalIndex, 5U);
    const auto* effectLow = FindSPro24OutputSignal(kSPro24BlockIns0, 8, kLow);
    ASSERT_NE(effectLow, nullptr);
    EXPECT_EQ(effectLow->kind, Kind::Auxiliary);
    EXPECT_EQ(effectLow->signalIndex, kSPro24AuxDestinationEffectSendFirst);

    // 2x: the same two channels now mean the opposite things.
    const auto* lineFiveMid = FindSPro24OutputSignal(kSPro24BlockIns0, 8, kMid);
    ASSERT_NE(lineFiveMid, nullptr);
    EXPECT_EQ(lineFiveMid->kind, Kind::AnalogLine);
    EXPECT_EQ(lineFiveMid->signalIndex, 5U);
    const auto* effectMid = FindSPro24OutputSignal(kSPro24BlockIns0, 4, kMid);
    ASSERT_NE(effectMid, nullptr);
    EXPECT_EQ(effectMid->kind, Kind::Auxiliary);
    EXPECT_EQ(effectMid->signalIndex, kSPro24AuxDestinationEffectSendFirst);

    // The reverb send moves too, and its 1x channels mean nothing at 2x.
    const auto* reverbMid = FindSPro24OutputSignal(kSPro24BlockIns0, 6, kMid);
    ASSERT_NE(reverbMid, nullptr);
    EXPECT_EQ(reverbMid->signalIndex, kSPro24AuxDestinationReverbSendFirst);
    EXPECT_EQ(FindSPro24OutputSignal(kSPro24BlockIns0, 14, kMid), nullptr);
}

TEST(SPro24SignalTableTests, SignalsAbsentAtARateAreUnreachableThere) {
    // ADAT In 5-8 vanish at 2x -- the ordinary S/MUX halving.
    ASSERT_NE(FindSPro24InputSignal(kSPro24BlockAdat, 4, kLow), nullptr);
    EXPECT_EQ(FindSPro24InputSignal(kSPro24BlockAdat, 4, kMid), nullptr);

    // ToHost11-14 likewise, and Loop. 1/2 moves into the channels they leave.
    ASSERT_NE(FindSPro24OutputSignal(kSPro24BlockAvs0, 10, kLow), nullptr);
    EXPECT_EQ(FindSPro24OutputSignal(kSPro24BlockAvs0, 10, kLow)->kind, Kind::HostStream);
    const auto* loopMid = FindSPro24OutputSignal(kSPro24BlockAvs0, 10, kMid);
    ASSERT_NE(loopMid, nullptr);
    EXPECT_EQ(loopMid->signalIndex, kSPro24AuxDestinationLoopbackFirst);

    // The device publishes 44.1/48/88.2/96 kHz only, so nothing resolves at the
    // High mode rather than the low column being assumed to repeat.
    EXPECT_EQ(FindSPro24OutputSignal(kSPro24BlockIns0, 0, kHigh), nullptr);
    EXPECT_EQ(FindSPro24InputSignal(kSPro24BlockIns0, 2, kHigh), nullptr);
}

TEST(SPro24SignalTableTests, ReportsCoordinatesPerRateAndFailsClosedWhenAbsent) {
    const auto* lineFive = FindSPro24OutputSignal(kSPro24BlockIns0, 4, kLow);
    ASSERT_NE(lineFive, nullptr);
    uint8_t block = 0;
    uint8_t channel = 0;
    ASSERT_TRUE(SPro24SignalCoordinates(*lineFive, kLow, block, channel));
    EXPECT_EQ(block, kSPro24BlockIns0);
    EXPECT_EQ(channel, 4U);
    ASSERT_TRUE(SPro24SignalCoordinates(*lineFive, kMid, block, channel));
    EXPECT_EQ(channel, 8U);
    EXPECT_FALSE(SPro24SignalCoordinates(*lineFive, kHigh, block, channel));

    const auto* adatFive = FindSPro24InputSignal(kSPro24BlockAdat, 4, kLow);
    ASSERT_NE(adatFive, nullptr);
    EXPECT_FALSE(SPro24SignalCoordinates(*adatFive, kMid, block, channel));
}

// The two directions are separate index spaces: the same auxiliary number means
// a different thing depending on which table it came from.
TEST(SPro24SignalTableTests, SourceAndDestinationAuxiliaryIndicesAreIndependent) {
    const auto* effectReturn = FindSPro24InputSignal(kSPro24BlockIns0, 8, kLow);
    ASSERT_NE(effectReturn, nullptr);
    EXPECT_EQ(effectReturn->signalIndex, kSPro24AuxSourceEffectReturnFirst);

    const auto* effectSend = FindSPro24OutputSignal(kSPro24BlockIns0, 8, kLow);
    ASSERT_NE(effectSend, nullptr);
    EXPECT_EQ(effectSend->signalIndex, kSPro24AuxDestinationEffectSendFirst);

    // Same channel, same rate, same auxiliary index -- opposite meanings.
    EXPECT_EQ(effectReturn->signalIndex, effectSend->signalIndex);
    EXPECT_NE(effectReturn, effectSend);
}

} // namespace
} // namespace ASFW::Audio::DICE::Focusrite
