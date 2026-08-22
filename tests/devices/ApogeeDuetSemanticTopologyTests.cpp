// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include <gtest/gtest.h>

#include "ASFWDriver/Audio/Protocols/Oxford/Apogee/ApogeeDuetSemanticTopology.hpp"

namespace {

using ASFW::Audio::AudioSemanticParameterKind;
using ASFW::Audio::AudioSemanticCrosspointPresentation;
using ASFW::Audio::AudioSemanticCrosspointGroup;
using ASFW::Audio::AudioSemanticTargetKind;
using ASFW::Audio::AudioSemanticTopologySnapshot;
using ASFW::Audio::Oxford::Apogee::BuildApogeeDuetSemanticTopology;
using ASFW::Audio::Oxford::Apogee::kApogeeDuetSemanticDeviceKind;

TEST(ApogeeDuetSemanticTopology, PublishesTheCompleteSignalGraph) {
    AudioSemanticTopologySnapshot topology{};
    ASSERT_TRUE(BuildApogeeDuetSemanticTopology(topology));

    EXPECT_EQ(topology.deviceKind, kApogeeDuetSemanticDeviceKind);
    EXPECT_EQ(topology.nodeCount, 8U);
    EXPECT_EQ(topology.portCount, 38U);
    EXPECT_EQ(topology.fixedLinkCount, 22U);
    EXPECT_EQ(topology.routerCount, 2U);
    EXPECT_EQ(topology.routeBundleCount, 6U);
    EXPECT_EQ(topology.routeCount, 8U);
    EXPECT_EQ(topology.crosspointCount, 8U);
    EXPECT_EQ(topology.parameterCount, 26U);
    EXPECT_EQ(topology.meterCount, 6U);
}

TEST(ApogeeDuetSemanticTopology, PublishesDeviceSpecificRoutingAndHardwareControls) {
    AudioSemanticTopologySnapshot topology{};
    ASSERT_TRUE(BuildApogeeDuetSemanticTopology(topology));

    EXPECT_EQ(topology.parameters[18].kind, AudioSemanticParameterKind::Source);
    EXPECT_EQ(topology.parameters[20].kind, AudioSemanticParameterKind::Source);
    EXPECT_EQ(topology.parameters[21].kind, AudioSemanticParameterKind::NominalLevel);
    EXPECT_EQ(topology.parameters[22].kind, AudioSemanticParameterKind::StereoLink);
    EXPECT_EQ(topology.parameters[23].targetKind, AudioSemanticTargetKind::Device);
    EXPECT_EQ(topology.parameters[23].kind, AudioSemanticParameterKind::HardwareControlTarget);
    EXPECT_EQ(topology.parameters[24].kind, AudioSemanticParameterKind::MuteFollow);
    EXPECT_EQ(topology.parameters[24].targetId, 61U);
    EXPECT_EQ(topology.parameters[25].kind, AudioSemanticParameterKind::MuteFollow);
    EXPECT_EQ(topology.parameters[25].targetId, 63U);
    EXPECT_EQ(topology.parameters[0].minimum, 10);
    EXPECT_EQ(topology.parameters[0].maximum, 75);
    for (size_t index = 0; index < topology.meterCount; ++index) {
        EXPECT_EQ(topology.meters[index].maximum, 0x3fff);
    }
}

TEST(ApogeeDuetSemanticTopology, PreservesIndependentInputsAndCoupledStereoOutput) {
    AudioSemanticTopologySnapshot topology{};
    ASSERT_TRUE(BuildApogeeDuetSemanticTopology(topology));

    // Input selector bundles are independent one-route choices.
    for (size_t index = 0; index < 4; ++index) {
        EXPECT_EQ(topology.routeBundles[index].routerNodeId, 2U);
        EXPECT_EQ(topology.routeBundles[index].routeCount, 1U);
    }
    // Output selector options are coupled left/right pairs.
    for (size_t index = 4; index < 6; ++index) {
        EXPECT_EQ(topology.routeBundles[index].routerNodeId, 6U);
        EXPECT_EQ(topology.routeBundles[index].routeCount, 2U);
    }
}

TEST(ApogeeDuetSemanticTopology, PublishesEightNativeMixerCoefficients) {
    AudioSemanticTopologySnapshot topology{};
    ASSERT_TRUE(BuildApogeeDuetSemanticTopology(topology));

    for (size_t index = 0; index < 8; ++index) {
        const auto& crosspoint = topology.crosspoints[index];
        const auto& parameter = topology.parameters[10 + index];
        EXPECT_EQ(crosspoint.id, index + 1U);
        EXPECT_EQ(parameter.targetKind, AudioSemanticTargetKind::Crosspoint);
        EXPECT_EQ(parameter.targetId, crosspoint.id);
        EXPECT_EQ(parameter.kind, AudioSemanticParameterKind::Level);
    }
}

TEST(ApogeeDuetSemanticTopology, PublishesStereoPlotHintsForEveryCrosspoint) {
    AudioSemanticTopologySnapshot topology{};
    ASSERT_TRUE(BuildApogeeDuetSemanticTopology(topology));

    EXPECT_EQ(topology.crosspoints[0].presentation, AudioSemanticCrosspointPresentation::PrimaryFader);
    EXPECT_EQ(topology.crosspoints[0].presentationGroup, AudioSemanticCrosspointGroup::InputMonitor);
    EXPECT_EQ(topology.crosspoints[0].presentationOrder, 0U);
    EXPECT_EQ(topology.crosspoints[5].presentation, AudioSemanticCrosspointPresentation::PrimaryFader);
    EXPECT_EQ(topology.crosspoints[5].presentationGroup, AudioSemanticCrosspointGroup::InputMonitor);
    EXPECT_EQ(topology.crosspoints[5].presentationOrder, 1U);
    EXPECT_EQ(topology.crosspoints[1].presentation, AudioSemanticCrosspointPresentation::RoutingFader);
    EXPECT_EQ(topology.crosspoints[6].presentation, AudioSemanticCrosspointPresentation::RoutingFader);
}

} // namespace
