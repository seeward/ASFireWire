// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Protocols/Oxford/Apogee/ApogeeDuetSemanticTopology.hpp"
#include "ASFWDriver/Audio/Shared/Topology/IAudioSemanticTopology.hpp"

namespace ASFW::Audio::Oxford::Apogee {
namespace {

AudioSemanticTopologySnapshot ValidDuetTopology() {
    AudioSemanticTopologySnapshot snapshot{};
    EXPECT_TRUE(BuildApogeeDuetSemanticTopology(snapshot));
    snapshot.topologyRevision = 1;
    return snapshot;
}

TEST(AudioSemanticTopologyValidationTests, AcceptsPublishedDuetTopology) {
    const auto snapshot = ValidDuetTopology();
    EXPECT_TRUE(ValidateAudioSemanticTopology(snapshot).has_value());
}

TEST(AudioSemanticTopologyValidationTests, RejectsForeignPortOwner) {
    auto snapshot = ValidDuetTopology();
    snapshot.ports[0].ownerNodeId = 0xDEAD;

    const auto result = ValidateAudioSemanticTopology(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticTopologyValidationError::MissingPortOwner);
}

TEST(AudioSemanticTopologyValidationTests, RejectsARepeatedCrosspointEdge) {
    auto snapshot = ValidDuetTopology();
    snapshot.crosspoints[1].sourcePortId = snapshot.crosspoints[0].sourcePortId;
    snapshot.crosspoints[1].destinationPortId = snapshot.crosspoints[0].destinationPortId;

    const auto result = ValidateAudioSemanticTopology(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticTopologyValidationError::DuplicateCrosspoint);
}

TEST(AudioSemanticTopologyValidationTests, RejectsAParameterOutsideItsDeclaredDomain) {
    auto snapshot = ValidDuetTopology();
    snapshot.parameters[0].minimum = 76;

    const auto result = ValidateAudioSemanticTopology(snapshot);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AudioSemanticTopologyValidationError::InvalidParameter);
}

} // namespace
} // namespace ASFW::Audio::Oxford::Apogee
