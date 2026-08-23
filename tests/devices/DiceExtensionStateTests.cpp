#include <gtest/gtest.h>

#include "Audio/Protocols/DICE/Core/DICEExtensionState.hpp"
#include "Common/WireFormat.hpp"

#include <array>

namespace {

using ASFW::Audio::DICE::DecodeDiceExtensionCaps;
using ASFW::Audio::DICE::DecodeDiceMixerCoefficients;
using ASFW::Audio::DICE::DecodeDiceRouterEntry;
using ASFW::Audio::DICE::DiceExtensionCaps;
using ASFW::Audio::DICE::DiceMixerCoefficients;
using ASFW::Audio::DICE::DiceRouterEntry;
using ASFW::Audio::DICE::kDiceMixerCoefficientWireBytes;

TEST(DiceExtensionStateTests, DecodesCapabilityWordsWithoutProfileAssumptions) {
    std::array<uint8_t, DiceExtensionCaps::kWireSize> wire{};
    ASFW::FW::WriteBE32(wire.data(), 0x00800007U);
    ASFW::FW::WriteBE32(wire.data() + 4, 0x100E2115U);
    ASFW::FW::WriteBE32(wire.data() + 8, 0x00001B17U);

    DiceExtensionCaps caps{};
    ASSERT_TRUE(DecodeDiceExtensionCaps(wire, caps));
    EXPECT_TRUE(caps.router.exposed);
    EXPECT_TRUE(caps.router.readOnly);
    EXPECT_TRUE(caps.router.storable);
    EXPECT_EQ(caps.router.maximumEntryCount, 128);
    EXPECT_TRUE(caps.mixer.exposed);
    EXPECT_FALSE(caps.mixer.readOnly);
    EXPECT_TRUE(caps.mixer.storable);
    EXPECT_EQ(caps.mixer.inputDeviceId, 1);
    EXPECT_EQ(caps.mixer.outputDeviceId, 1);
    EXPECT_EQ(caps.mixer.inputCount, 14);
    EXPECT_EQ(caps.mixer.outputCount, 16);
    EXPECT_TRUE(caps.general.dynamicStreamFormat);
    EXPECT_TRUE(caps.general.storageAvailable);
    EXPECT_TRUE(caps.general.peakAvailable);
    EXPECT_EQ(caps.general.maximumTxStreams, 1);
    EXPECT_EQ(caps.general.maximumRxStreams, 11);
    EXPECT_TRUE(caps.general.streamFormatStorable);
}

TEST(DiceExtensionStateTests, DecodesRouterAndPeakPacking) {
    std::array<uint8_t, DiceRouterEntry::kWireSize> wire{};
    // Peak 0x7F00, source AVS0:1, destination INS0:2.
    ASFW::FW::WriteBE32(wire.data(), 0x7F00B142U);

    DiceRouterEntry entry{};
    ASSERT_TRUE(DecodeDiceRouterEntry(wire, entry));
    EXPECT_EQ(entry.destinationBlock, 4);
    EXPECT_EQ(entry.destinationChannel, 2);
    EXPECT_EQ(entry.sourceBlock, 11);
    EXPECT_EQ(entry.sourceChannel, 1);
    EXPECT_EQ(entry.peak, 0x7F00);
}

TEST(DiceExtensionStateTests, DecodesOnlyTheCapabilityDeclaredMixerArea) {
    std::array<uint8_t, kDiceMixerCoefficientWireBytes> wire{};
    // The fixed wire stride remains 18 inputs even for a 2 x 2 active matrix.
    ASFW::FW::WriteBE32(wire.data() + 0, 0x00001234U);
    ASFW::FW::WriteBE32(wire.data() + 4, 0x00005678U);
    ASFW::FW::WriteBE32(wire.data() + 72, 0x00009ABCU);

    DiceExtensionCaps caps{};
    caps.mixer.exposed = true;
    caps.mixer.inputCount = 2;
    caps.mixer.outputCount = 2;
    DiceMixerCoefficients matrix{};
    ASSERT_TRUE(DecodeDiceMixerCoefficients(wire, caps.mixer, matrix));
    EXPECT_EQ(matrix.inputCount, 2);
    EXPECT_EQ(matrix.outputCount, 2);
    EXPECT_EQ(matrix.At(0, 0), 0x1234);
    EXPECT_EQ(matrix.At(0, 1), 0x5678);
    EXPECT_EQ(matrix.At(1, 0), 0x9ABC);
    EXPECT_EQ(matrix.At(1, 1), 0);
}

TEST(DiceExtensionStateTests, RejectsTruncatedOrImpossibleData) {
    DiceExtensionCaps caps{};
    EXPECT_FALSE(DecodeDiceExtensionCaps({}, caps));

    DiceRouterEntry entry{};
    EXPECT_FALSE(DecodeDiceRouterEntry({}, entry));

    std::array<uint8_t, kDiceMixerCoefficientWireBytes> wire{};
    caps.mixer.inputCount = 19;
    caps.mixer.outputCount = 1;
    DiceMixerCoefficients matrix{};
    EXPECT_FALSE(DecodeDiceMixerCoefficients(wire, caps.mixer, matrix));
}

} // namespace
