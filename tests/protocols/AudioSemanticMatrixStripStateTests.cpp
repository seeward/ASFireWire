// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Shared/Topology/AudioSemanticMatrixStripStates.hpp"

namespace ASFW::Audio {
namespace {

constexpr uint32_t kBusOne = 0x9001;
constexpr uint32_t kBusTwo = 0x9002;
constexpr uint32_t kMonoSource = 0x8001;
constexpr uint32_t kStereoSource = 0x8002;
constexpr uint32_t kHiddenSource = 0x8003;

/// One stereo destination pair fed by a mono source, a stereo source, and a
/// source whose cells the profile declared hidden.
[[nodiscard]] AudioSemanticMatrixSnapshot Fixture() {
    AudioSemanticMatrixSnapshot snapshot{};
    snapshot.deviceKind = 1;
    snapshot.topologyRevision = 1;
    snapshot.kind = AudioSemanticMatrixKind::Mixer;
    snapshot.coefficientMaximum = 65535;
    snapshot.gainLaw = AudioSemanticMatrixGainLaw::UnsignedQ214Amplitude;
    snapshot.inputCount = 4;
    snapshot.outputCount = 4;

    snapshot.inputs[0] = {.portId = 1, .signalKind = AudioSemanticSignalKind::AnalogLine,
                          .signalIndex = 1, .presentationGroupId = kMonoSource,
                          .channelRole = AudioSemanticMatrixChannelRole::Mono};
    snapshot.inputs[1] = {.portId = 2, .signalKind = AudioSemanticSignalKind::HostStream,
                          .signalIndex = 1, .presentationGroupId = kStereoSource,
                          .channelRole = AudioSemanticMatrixChannelRole::Left};
    snapshot.inputs[2] = {.portId = 3, .signalKind = AudioSemanticSignalKind::HostStream,
                          .signalIndex = 2, .presentationGroupId = kStereoSource,
                          .channelRole = AudioSemanticMatrixChannelRole::Right};
    snapshot.inputs[3] = {.portId = 4, .signalKind = AudioSemanticSignalKind::Auxiliary,
                          .signalIndex = 3, .presentationGroupId = kHiddenSource,
                          .channelRole = AudioSemanticMatrixChannelRole::Mono};

    snapshot.outputs[0] = {.portId = 11, .signalKind = AudioSemanticSignalKind::Auxiliary,
                           .signalIndex = 1, .presentationGroupId = kBusOne,
                           .channelRole = AudioSemanticMatrixChannelRole::Left,
                           .outputRole = AudioSemanticMatrixOutputRole::MonitorMix};
    snapshot.outputs[1] = {.portId = 12, .signalKind = AudioSemanticSignalKind::Auxiliary,
                           .signalIndex = 2, .presentationGroupId = kBusOne,
                           .channelRole = AudioSemanticMatrixChannelRole::Right,
                           .outputRole = AudioSemanticMatrixOutputRole::MonitorMix};
    snapshot.outputs[2] = {.portId = 13, .signalKind = AudioSemanticSignalKind::Auxiliary,
                           .signalIndex = 1, .presentationGroupId = kBusTwo,
                           .channelRole = AudioSemanticMatrixChannelRole::Left,
                           .outputRole = AudioSemanticMatrixOutputRole::EffectSend};
    snapshot.outputs[3] = {.portId = 14, .signalKind = AudioSemanticSignalKind::Auxiliary,
                           .signalIndex = 2, .presentationGroupId = kBusTwo,
                           .channelRole = AudioSemanticMatrixChannelRole::Right,
                           .outputRole = AudioSemanticMatrixOutputRole::EffectSend};

    const auto set = [&](uint32_t output, uint32_t input, uint16_t value,
                         AudioSemanticMatrixCrosspointPresentation presentation) {
        const size_t index = size_t{output} * kMaxAudioSemanticMatrixInputs + input;
        snapshot.coefficients[index] = value;
        snapshot.crosspointPresentations[index] = presentation;
    };
    using Presentation = AudioSemanticMatrixCrosspointPresentation;
    for (uint32_t output = 0; output < 4; ++output) {
        set(output, 0, 0x2000, Presentation::MonoLevelPan);
        set(output, 3, 0x1000, Presentation::Hidden);
    }
    // A stereo strip owns only its diagonal cells; the off-diagonal pair is not
    // part of the linked gesture.
    set(0, 1, 0x4000, Presentation::StereoLevelBalance);
    set(1, 2, 0x3000, Presentation::StereoLevelBalance);
    set(2, 1, 0x4000, Presentation::StereoLevelBalance);
    set(3, 2, 0x3000, Presentation::StereoLevelBalance);
    set(0, 2, 0, Presentation::Hidden);
    set(1, 1, 0, Presentation::Hidden);
    set(2, 2, 0, Presentation::Hidden);
    set(3, 1, 0, Presentation::Hidden);
    return snapshot;
}

TEST(AudioSemanticMatrixStripStateTests, ResolvesMonoAndStereoStripsAndRefusesHiddenCells) {
    const auto snapshot = Fixture();

    AudioSemanticMatrixStripCells mono{};
    ASSERT_TRUE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, kMonoSource, mono));
    EXPECT_TRUE(mono.IsMono());
    EXPECT_EQ(mono.inputLeft, 0U);
    EXPECT_EQ(mono.inputRight, 0U);
    EXPECT_EQ(mono.outputLeft, 0U);
    EXPECT_EQ(mono.outputRight, 1U);

    AudioSemanticMatrixStripCells stereo{};
    ASSERT_TRUE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, kStereoSource, stereo));
    EXPECT_FALSE(stereo.IsMono());
    EXPECT_EQ(stereo.inputLeft, 1U);
    EXPECT_EQ(stereo.inputRight, 2U);

    // A source whose cells the profile hid is not a strip, even though both
    // groups exist and the coefficients are perfectly readable.
    AudioSemanticMatrixStripCells hidden{};
    EXPECT_FALSE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, kHiddenSource, hidden));
    EXPECT_FALSE(ResolveAudioSemanticMatrixStrip(snapshot, 0, kMonoSource, hidden));
    EXPECT_FALSE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, 0xdead, hidden));
}

TEST(AudioSemanticMatrixStripStateTests, EnumeratesOnlyWritableStripsOnOneBus) {
    const auto snapshot = Fixture();
    std::array<AudioSemanticMatrixStripCells, kMaxAudioSemanticMatrixStripsPerBus> strips{};
    uint32_t count = 0;
    ASSERT_TRUE(EnumerateAudioSemanticMatrixBusStrips(snapshot, kBusOne, strips, count));
    // The stereo source contributes one strip, not two, and the hidden source
    // contributes none.
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(strips[0].inputPresentationGroupId, kMonoSource);
    EXPECT_EQ(strips[1].inputPresentationGroupId, kStereoSource);

    uint32_t missing = 0;
    EXPECT_FALSE(EnumerateAudioSemanticMatrixBusStrips(snapshot, 0xdead, strips, missing));
    EXPECT_EQ(missing, 0U);
}

TEST(AudioSemanticMatrixStripStateTests, MuteSuppressesOneStripAndRemembersItsNominal) {
    const auto snapshot = Fixture();
    AudioSemanticMatrixStripCells mono{};
    ASSERT_TRUE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, kMonoSource, mono));

    AudioSemanticMatrixStripStateSet states;
    // With no record, the nominal is simply what the hardware holds -- which is
    // what makes the very first mute of a strip reversible.
    const auto before = AudioSemanticMatrixStripEffective(snapshot, states, mono);
    EXPECT_EQ(before.left, 0x2000);
    EXPECT_EQ(before.right, 0x2000);

    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kMonoSource,
                               .nominalLeft = 0x2000, .nominalRight = 0x2000,
                               .muted = 1}));
    const auto muted = AudioSemanticMatrixStripEffective(snapshot, states, mono);
    EXPECT_EQ(muted.left, 0);
    EXPECT_EQ(muted.right, 0);
    const auto nominal = AudioSemanticMatrixStripNominal(snapshot, states, mono);
    EXPECT_EQ(nominal.left, 0x2000);
    EXPECT_EQ(nominal.right, 0x2000);

    // Another strip on the same bus is untouched by a mute.
    AudioSemanticMatrixStripCells stereo{};
    ASSERT_TRUE(ResolveAudioSemanticMatrixStrip(snapshot, kBusOne, kStereoSource, stereo));
    EXPECT_EQ(AudioSemanticMatrixStripEffective(snapshot, states, stereo).left, 0x4000);
}

TEST(AudioSemanticMatrixStripStateTests, SoloSuppressesEveryOtherStripOnItsBusOnly) {
    const auto snapshot = Fixture();
    AudioSemanticMatrixStripStateSet states;
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kStereoSource,
                               .nominalLeft = 0x4000, .nominalRight = 0x3000,
                               .soloed = 1}));

    EXPECT_TRUE(states.BusHasSolo(kBusOne));
    EXPECT_FALSE(states.BusHasSolo(kBusTwo));
    EXPECT_FALSE(states.IsSuppressed(kBusOne, kStereoSource));
    // Suppressed without a record of its own: solo is a property of the bus.
    EXPECT_TRUE(states.IsSuppressed(kBusOne, kMonoSource));
    // The same source on a different bus keeps playing.
    EXPECT_FALSE(states.IsSuppressed(kBusTwo, kMonoSource));

    // An explicit mute still wins on the soloed strip itself, so clearing the
    // solo cannot resurrect something the user muted.
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kStereoSource,
                               .nominalLeft = 0x4000, .nominalRight = 0x3000,
                               .muted = 1, .soloed = 1}));
    EXPECT_TRUE(states.IsSuppressed(kBusOne, kStereoSource));
}

TEST(AudioSemanticMatrixStripStateTests, PruneKeepsRecordsThatStillHoldANominal) {
    AudioSemanticMatrixStripStateSet states;
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kStereoSource,
                               .nominalLeft = 0x4000, .nominalRight = 0x3000,
                               .soloed = 1}));
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kMonoSource,
                               .nominalLeft = 0x2000, .nominalRight = 0x2000}));
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusTwo,
                               .inputPresentationGroupId = kMonoSource,
                               .nominalLeft = 0x2000, .nominalRight = 0x2000}));
    ASSERT_EQ(states.Count(), 3U);

    // Bus two has no solo, so its plain record carries nothing and goes. The
    // bus-one record is solo-suppressed and must keep its nominal.
    states.Prune();
    ASSERT_EQ(states.Count(), 2U);
    EXPECT_NE(states.Find(kBusOne, kMonoSource), nullptr);
    EXPECT_EQ(states.Find(kBusTwo, kMonoSource), nullptr);

    states.Remove(kBusOne, kStereoSource);
    states.Prune();
    EXPECT_EQ(states.Count(), 0U);
}

TEST(AudioSemanticMatrixStripStateTests, UpsertFailsClosedAtTheBoundRatherThanDroppingANominal) {
    AudioSemanticMatrixStripStateSet states;
    for (uint32_t index = 0; index < kMaxAudioSemanticMatrixStripStates; ++index) {
        ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                                   .inputPresentationGroupId = 0x1000 + index,
                                   .nominalLeft = 0x4000, .nominalRight = 0x4000,
                                   .muted = 1}));
    }
    EXPECT_EQ(states.Count(), kMaxAudioSemanticMatrixStripStates);
    // A dropped record is a nominal level nothing can restore, so the bound
    // refuses new records instead of evicting one.
    EXPECT_FALSE(states.Upsert({.outputPresentationGroupId = kBusTwo,
                                .inputPresentationGroupId = 0x2000,
                                .nominalLeft = 0x4000, .nominalRight = 0x4000,
                                .muted = 1}));
    // Replacing an existing record still works at the bound.
    EXPECT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = 0x1000,
                               .nominalLeft = 0x1234, .nominalRight = 0x1234,
                               .muted = 1}));
    ASSERT_NE(states.Find(kBusOne, 0x1000), nullptr);
    EXPECT_EQ(states.Find(kBusOne, 0x1000)->nominalLeft, 0x1234);
    EXPECT_FALSE(states.Upsert({.outputPresentationGroupId = 0,
                                .inputPresentationGroupId = 0x1000}));
}

TEST(AudioSemanticMatrixStripStateTests, PublishedSnapshotAnswersTheSameSuppressionQuestions) {
    auto snapshot = Fixture();
    AudioSemanticMatrixStripStateSet states;
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kStereoSource,
                               .nominalLeft = 0x4000, .nominalRight = 0x3000,
                               .soloed = 1}));
    ASSERT_TRUE(states.Upsert({.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kMonoSource,
                               .nominalLeft = 0x2000, .nominalRight = 0x2000}));
    states.CopyInto(snapshot);

    ASSERT_EQ(snapshot.stripStateCount, 2U);
    EXPECT_TRUE(snapshot.BusHasSolo(kBusOne));
    EXPECT_FALSE(snapshot.BusHasSolo(kBusTwo));
    EXPECT_TRUE(snapshot.StripIsSuppressed(kBusOne, kMonoSource));
    EXPECT_FALSE(snapshot.StripIsSuppressed(kBusOne, kStereoSource));
    ASSERT_NE(snapshot.StripState(kBusOne, kMonoSource), nullptr);
    EXPECT_EQ(snapshot.StripState(kBusOne, kMonoSource)->nominalLeft, 0x2000);
    EXPECT_EQ(snapshot.StripState(kBusTwo, kMonoSource), nullptr);
    EXPECT_TRUE(ValidateAudioSemanticMatrix(snapshot).has_value());
}

TEST(AudioSemanticMatrixStripStateTests, ValidationRejectsMalformedStripStates) {
    using Error = AudioSemanticMatrixValidationError;
    auto snapshot = Fixture();
    snapshot.stripStateCount = kMaxAudioSemanticMatrixStripStates + 1;
    auto result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::StripStateCountOutOfRange);

    snapshot = Fixture();
    snapshot.stripStateCount = 1;
    snapshot.stripStates[0] = {.outputPresentationGroupId = 0,
                               .inputPresentationGroupId = kMonoSource};
    result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::InvalidStripState);

    snapshot = Fixture();
    snapshot.stripStateCount = 2;
    snapshot.stripStates[0] = {.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kMonoSource, .muted = 1};
    snapshot.stripStates[1] = {.outputPresentationGroupId = kBusOne,
                               .inputPresentationGroupId = kMonoSource, .soloed = 1};
    result = ValidateAudioSemanticMatrix(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::DuplicateStripState);
}

} // namespace
} // namespace ASFW::Audio
