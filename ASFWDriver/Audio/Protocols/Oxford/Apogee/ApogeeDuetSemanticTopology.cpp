// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project

#include "ApogeeDuetSemanticTopology.hpp"

#include <limits>

namespace ASFW::Audio::Oxford::Apogee {

namespace {

constexpr int32_t kNormalizedOne = 1'000'000;
constexpr int32_t kNormalized14BitStep = kNormalizedOne / 16'383;

} // namespace

bool BuildApogeeDuetSemanticTopology(AudioSemanticTopologySnapshot& outSnapshot) noexcept {
    using NodeKind = AudioSemanticNodeKind;
    using EndpointKind = AudioSemanticEndpointKind;
    using PortDirection = AudioSemanticPortDirection;
    using SignalKind = AudioSemanticSignalKind;
    using TargetKind = AudioSemanticTargetKind;
    using ParameterKind = AudioSemanticParameterKind;
    using ValueKind = AudioSemanticValueKind;
    using Unit = AudioSemanticUnit;
    using Presentation = AudioSemanticPresentation;
    using MeterKind = AudioSemanticMeterKind;
    using MeterUnit = AudioSemanticMeterUnit;

    outSnapshot = {};
    outSnapshot.deviceKind = kApogeeDuetSemanticDeviceKind;

    constexpr AudioSemanticNode kNodes[] = {
        {1, NodeKind::Endpoint, EndpointKind::Physical}, {2, NodeKind::Router, EndpointKind::None},
        {3, NodeKind::Processor, EndpointKind::None}, {4, NodeKind::Endpoint, EndpointKind::Host},
        {5, NodeKind::Mixer, EndpointKind::None}, {6, NodeKind::Router, EndpointKind::None},
        {7, NodeKind::Processor, EndpointKind::None}, {8, NodeKind::Endpoint, EndpointKind::Physical},
    };
    constexpr AudioSemanticPort kPorts[] = {
        {1, 1, PortDirection::Output, SignalKind::AnalogMicXlr, 1},
        {2, 1, PortDirection::Output, SignalKind::AnalogMicXlr, 2},
        {3, 1, PortDirection::Output, SignalKind::AnalogInstrument, 1},
        {4, 1, PortDirection::Output, SignalKind::AnalogInstrument, 2},
        {11, 2, PortDirection::Input, SignalKind::None, 0}, {12, 2, PortDirection::Input, SignalKind::None, 0},
        {13, 2, PortDirection::Input, SignalKind::None, 0}, {14, 2, PortDirection::Input, SignalKind::None, 0},
        {15, 2, PortDirection::Output, SignalKind::None, 0}, {16, 2, PortDirection::Output, SignalKind::None, 0},
        {21, 3, PortDirection::Input, SignalKind::None, 0}, {22, 3, PortDirection::Input, SignalKind::None, 0},
        {23, 3, PortDirection::Output, SignalKind::None, 0}, {24, 3, PortDirection::Output, SignalKind::None, 0},
        {31, 4, PortDirection::Input, SignalKind::HostStream, 1}, {32, 4, PortDirection::Input, SignalKind::HostStream, 2},
        {33, 4, PortDirection::Output, SignalKind::HostStream, 1}, {34, 4, PortDirection::Output, SignalKind::HostStream, 2},
        {41, 5, PortDirection::Input, SignalKind::None, 0}, {42, 5, PortDirection::Input, SignalKind::None, 0},
        {43, 5, PortDirection::Input, SignalKind::None, 0}, {44, 5, PortDirection::Input, SignalKind::None, 0},
        {45, 5, PortDirection::Output, SignalKind::None, 0}, {46, 5, PortDirection::Output, SignalKind::None, 0},
        {51, 6, PortDirection::Input, SignalKind::None, 0}, {52, 6, PortDirection::Input, SignalKind::None, 0},
        {53, 6, PortDirection::Input, SignalKind::None, 0}, {54, 6, PortDirection::Input, SignalKind::None, 0},
        {55, 6, PortDirection::Output, SignalKind::None, 0}, {56, 6, PortDirection::Output, SignalKind::None, 0},
        {57, 7, PortDirection::Input, SignalKind::None, 0}, {58, 7, PortDirection::Input, SignalKind::None, 0},
        {59, 7, PortDirection::Output, SignalKind::None, 0}, {60, 7, PortDirection::Output, SignalKind::None, 0},
        {61, 8, PortDirection::Input, SignalKind::AnalogLine, 1}, {62, 8, PortDirection::Input, SignalKind::AnalogLine, 2},
        {63, 8, PortDirection::Input, SignalKind::Headphone, 1}, {64, 8, PortDirection::Input, SignalKind::Headphone, 2},
    };
    constexpr AudioSemanticFixedLink kFixedLinks[] = {
        {1, 11}, {3, 12}, {2, 13}, {4, 14}, {15, 21}, {16, 22}, {23, 31}, {24, 32},
        {23, 41}, {24, 42}, {33, 43}, {34, 44}, {33, 51}, {34, 52}, {45, 53}, {46, 54},
        {55, 57}, {56, 58}, {59, 61}, {60, 62}, {59, 63}, {60, 64},
    };
    constexpr AudioSemanticRouter kRouters[] = {{2, 2, 1, 1}, {6, 1, 1, 1}};
    constexpr AudioSemanticRouteBundle kBundles[] = {
        {2, 1, 0, 1}, {2, 2, 1, 1}, {2, 3, 2, 1}, {2, 4, 3, 1}, {6, 1, 4, 2}, {6, 2, 6, 2},
    };
    constexpr AudioSemanticRoute kRoutes[] = {
        {11, 15}, {12, 15}, {13, 16}, {14, 16}, {51, 55}, {52, 56}, {53, 55}, {54, 56},
    };
    constexpr AudioSemanticCrosspoint kCrosspoints[] = {
        {1, 41, 45}, {2, 42, 45}, {3, 43, 45}, {4, 44, 45},
        {5, 41, 46}, {6, 42, 46}, {7, 43, 46}, {8, 44, 46},
    };
    constexpr AudioSemanticParameter kParameters[] = {
        {1, TargetKind::Port, 21, ParameterKind::Level, ValueKind::Scalar, Unit::Decibels, 10, 75, 1, Presentation::Fader},
        {2, TargetKind::Port, 22, ParameterKind::Level, ValueKind::Scalar, Unit::Decibels, 10, 75, 1, Presentation::Fader},
        {3, TargetKind::Port, 1, ParameterKind::PhantomPower, ValueKind::Boolean, Unit::None, 0, 1, 1, Presentation::Toggle},
        {4, TargetKind::Port, 2, ParameterKind::PhantomPower, ValueKind::Boolean, Unit::None, 0, 1, 1, Presentation::Toggle},
        {5, TargetKind::Port, 21, ParameterKind::PhaseInvert, ValueKind::Boolean, Unit::None, 0, 1, 1, Presentation::Toggle},
        {6, TargetKind::Port, 22, ParameterKind::PhaseInvert, ValueKind::Boolean, Unit::None, 0, 1, 1, Presentation::Toggle},
        {7, TargetKind::Port, 1, ParameterKind::NominalLevel, ValueKind::Enum, Unit::None, 0, 2, 1, Presentation::Selector},
        {8, TargetKind::Port, 2, ParameterKind::NominalLevel, ValueKind::Enum, Unit::None, 0, 2, 1, Presentation::Selector},
        {9, TargetKind::Port, 59, ParameterKind::Level, ValueKind::Scalar, Unit::Decibels, -64, 0, 1, Presentation::Fader},
        {10, TargetKind::Port, 59, ParameterKind::Mute, ValueKind::Boolean, Unit::None, 0, 1, 1, Presentation::Toggle},
        {11, TargetKind::Crosspoint, 1, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {12, TargetKind::Crosspoint, 2, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {13, TargetKind::Crosspoint, 3, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {14, TargetKind::Crosspoint, 4, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {15, TargetKind::Crosspoint, 5, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {16, TargetKind::Crosspoint, 6, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {17, TargetKind::Crosspoint, 7, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
        {18, TargetKind::Crosspoint, 8, ParameterKind::Level, ValueKind::Scalar, Unit::Normalized, 0, kNormalizedOne, kNormalized14BitStep, Presentation::Fader},
    };
    constexpr AudioSemanticMeter kMeters[] = {
        {1, 23, MeterKind::Level, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
        {2, 24, MeterKind::Level, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
        {3, 43, MeterKind::Level, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
        {4, 44, MeterKind::Level, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
        {5, 45, MeterKind::Peak, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
        {6, 46, MeterKind::Peak, MeterUnit::Native, 0, std::numeric_limits<int32_t>::max()},
    };

    static_assert(std::size(kNodes) <= kMaxAudioSemanticNodes);
    static_assert(std::size(kPorts) <= kMaxAudioSemanticPorts);
    static_assert(std::size(kFixedLinks) <= kMaxAudioSemanticFixedLinks);
    static_assert(std::size(kRouters) <= kMaxAudioSemanticRouters);
    static_assert(std::size(kBundles) <= kMaxAudioSemanticRouteBundles);
    static_assert(std::size(kRoutes) <= kMaxAudioSemanticRoutes);
    static_assert(std::size(kCrosspoints) <= kMaxAudioSemanticCrosspoints);
    static_assert(std::size(kParameters) <= kMaxAudioSemanticParameters);
    static_assert(std::size(kMeters) <= kMaxAudioSemanticMeters);

    for (size_t i = 0; i < std::size(kNodes); ++i) outSnapshot.nodes[i] = kNodes[i];
    for (size_t i = 0; i < std::size(kPorts); ++i) outSnapshot.ports[i] = kPorts[i];
    for (size_t i = 0; i < std::size(kFixedLinks); ++i) outSnapshot.fixedLinks[i] = kFixedLinks[i];
    for (size_t i = 0; i < std::size(kRouters); ++i) outSnapshot.routers[i] = kRouters[i];
    for (size_t i = 0; i < std::size(kBundles); ++i) outSnapshot.routeBundles[i] = kBundles[i];
    for (size_t i = 0; i < std::size(kRoutes); ++i) outSnapshot.routes[i] = kRoutes[i];
    for (size_t i = 0; i < std::size(kCrosspoints); ++i) outSnapshot.crosspoints[i] = kCrosspoints[i];
    for (size_t i = 0; i < std::size(kParameters); ++i) outSnapshot.parameters[i] = kParameters[i];
    for (size_t i = 0; i < std::size(kMeters); ++i) outSnapshot.meters[i] = kMeters[i];
    outSnapshot.nodeCount = std::size(kNodes);
    outSnapshot.portCount = std::size(kPorts);
    outSnapshot.fixedLinkCount = std::size(kFixedLinks);
    outSnapshot.routerCount = std::size(kRouters);
    outSnapshot.routeBundleCount = std::size(kBundles);
    outSnapshot.routeCount = std::size(kRoutes);
    outSnapshot.crosspointCount = std::size(kCrosspoints);
    outSnapshot.parameterCount = std::size(kParameters);
    outSnapshot.meterCount = std::size(kMeters);
    return true;
}

} // namespace ASFW::Audio::Oxford::Apogee
