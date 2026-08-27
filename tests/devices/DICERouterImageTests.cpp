// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "ASFWDriver/Audio/Protocols/DICE/Core/DICERouterImage.hpp"
#include "SPro24RouterImageFixture.hpp"

namespace ASFW::Audio::DICE {
namespace {

using Focusrite::Testing::kSPro24CapturedRouterImage;
using Focusrite::Testing::kSPro24CapturedRouterEntryCount;
using Focusrite::Testing::kSPro24ReservedMeterEntryCount;

/// Serialises the captured image the way the device presented it: a leading
/// count quadlet followed by one big-endian quadlet per entry.
[[nodiscard]] std::vector<uint8_t> CapturedWire() {
    std::vector<uint8_t> wire(DiceRouterImageWireBytes(kSPro24CapturedRouterEntryCount));
    const auto put = [&wire](size_t offset, uint32_t value) {
        wire[offset] = static_cast<uint8_t>(value >> 24U);
        wire[offset + 1] = static_cast<uint8_t>(value >> 16U);
        wire[offset + 2] = static_cast<uint8_t>(value >> 8U);
        wire[offset + 3] = static_cast<uint8_t>(value);
    };
    put(0, kSPro24CapturedRouterEntryCount);
    for (size_t index = 0; index < kSPro24CapturedRouterImage.size(); ++index) {
        put(4 + index * DiceRouterEntry::kWireSize, kSPro24CapturedRouterImage[index]);
    }
    return wire;
}

[[nodiscard]] DiceRouterEntries CapturedImage() {
    const auto wire = CapturedWire();
    DiceRouterEntries image{};
    EXPECT_TRUE(DecodeDiceRouterEntries(
        std::span<const uint8_t>(wire).subspan(4),
        static_cast<uint16_t>(kSPro24CapturedRouterEntryCount), image));
    return image;
}

TEST(DICERouterImageTests, DecodesTheCapturedHardwareImage) {
    const auto image = CapturedImage();
    ASSERT_EQ(image.count, 48U);

    // Entries 0-3 are the physical-input meter sources, in the order the ALSA
    // tcd22xx FIXED specification lays them out.
    EXPECT_EQ(image.At(0).sourceBlock, 4U);
    EXPECT_EQ(image.At(0).sourceChannel, 2U);
    EXPECT_EQ(image.At(1).sourceChannel, 3U);
    EXPECT_EQ(image.At(2).sourceChannel, 0U);
    EXPECT_EQ(image.At(3).sourceChannel, 1U);

    // Mixer inputs are fed through MixerTx0/MixerTx1 destinations.
    EXPECT_EQ(image.At(24).destinationBlock, 2U);
    EXPECT_EQ(image.At(24).destinationChannel, 0U);
    EXPECT_EQ(image.At(24).sourceBlock, 4U);
    EXPECT_EQ(image.At(24).sourceChannel, 2U); // Anlg In 1

    // The last two entries share one destination: they are metering slots.
    EXPECT_EQ(image.At(46).destinationBlock, 15U);
    EXPECT_EQ(image.At(46).destinationChannel, 0U);
    EXPECT_EQ(image.At(47).destinationBlock, 15U);
    EXPECT_EQ(image.At(47).destinationChannel, 0U);
}

// An unmodified image must re-encode to exactly the bytes it came from, or
// verifying a router write by reading it back proves nothing.
TEST(DICERouterImageTests, UnmodifiedImageRoundTripsToTheSameBytes) {
    const auto original = CapturedWire();
    const auto image = CapturedImage();

    std::vector<uint8_t> encoded(original.size());
    const auto written = EncodeDiceRouterImage(image, 128, encoded);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(*written, original.size());
    EXPECT_EQ(encoded, original);
}

TEST(DICERouterImageTests, ReplacesOneSourceInPlaceAndLeavesEveryOtherEntryAlone) {
    auto image = CapturedImage();
    const auto before = image;

    // Entry 16 drives Ins0:2 (Line out 3) from Mixer:0. Point it at Mixer:8.
    const auto index = FindDiceRouterDestination(image, 4, 2);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 16U);

    ASSERT_TRUE(SetDiceRouterSource(image, 4, 2, 2, 8, kSPro24ReservedMeterEntryCount)
                    .has_value());
    EXPECT_EQ(image.count, before.count);
    EXPECT_EQ(image.At(16).sourceBlock, 2U);
    EXPECT_EQ(image.At(16).sourceChannel, 8U);
    // Destination is untouched: an edit retargets a source, never moves a route.
    EXPECT_EQ(image.At(16).destinationBlock, 4U);
    EXPECT_EQ(image.At(16).destinationChannel, 2U);

    for (uint16_t entry = 0; entry < before.count; ++entry) {
        if (entry == 16) continue;
        EXPECT_EQ(image.At(entry).destinationBlock, before.At(entry).destinationBlock);
        EXPECT_EQ(image.At(entry).destinationChannel, before.At(entry).destinationChannel);
        EXPECT_EQ(image.At(entry).sourceBlock, before.At(entry).sourceBlock);
        EXPECT_EQ(image.At(entry).sourceChannel, before.At(entry).sourceChannel);
    }
    EXPECT_FALSE(DiceRouterImagesMatch(image, before));
}

TEST(DICERouterImageTests, RefusesTheReservedMeterEntriesAndAmbiguousDestinations) {
    auto image = CapturedImage();

    // Entry 0 drives Ins0:8 and lies inside the reserved metering range.
    auto result = SetDiceRouterSource(image, 4, 8, 2, 0, kSPro24ReservedMeterEntryCount);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DiceRouterEditError::ReservedEntry);

    // blk15:0 appears twice, so there is no single entry to retarget.
    result = SetDiceRouterSource(image, 15, 0, 2, 0, kSPro24ReservedMeterEntryCount);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DiceRouterEditError::AmbiguousDestination);

    // Creating a route would shift every later entry, and with it the peak
    // section's meaning, so it is refused rather than silently appended.
    result = SetDiceRouterSource(image, 1, 5, 2, 0, kSPro24ReservedMeterEntryCount);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DiceRouterEditError::DestinationNotPresent);

    EXPECT_TRUE(DiceRouterImagesMatch(image, CapturedImage()));
}

TEST(DICERouterImageTests, EncodeFailsClosedOutsideTheDeviceAdvertisedBound) {
    auto image = CapturedImage();
    std::vector<uint8_t> wire(DiceRouterImageWireBytes(48));

    // The device advertises maximum_entry_count; an image longer than that is
    // refused rather than truncated onto the wire.
    auto written = EncodeDiceRouterImage(image, 47, wire);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), DiceRouterEditError::OutOfRange);

    // A short buffer is refused for the same reason.
    std::vector<uint8_t> tooSmall(DiceRouterImageWireBytes(48) - 1);
    written = EncodeDiceRouterImage(image, 128, tooSmall);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), DiceRouterEditError::OutOfRange);

    image.count = kDiceMaximumRouterEntries + 1;
    written = EncodeDiceRouterImage(image, 128, wire);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error(), DiceRouterEditError::OutOfRange);
}

// The peak half is a live meter reading, not configuration, so two reads of an
// unchanged router must still compare equal.
TEST(DICERouterImageTests, ComparisonIgnoresLivePeakReadings) {
    const auto left = CapturedImage();
    auto right = CapturedImage();
    right.entries[10].peak = 0x1234;
    EXPECT_TRUE(DiceRouterImagesMatch(left, right));

    right.entries[10].sourceChannel = 9;
    EXPECT_FALSE(DiceRouterImagesMatch(left, right));
}

} // namespace
} // namespace ASFW::Audio::DICE
