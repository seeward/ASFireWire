// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// IAudioSemanticTopology.hpp -- bounded, protocol-neutral audio topology
// snapshot.  This is the driver/UI seam: implementations describe signal
// truth, never vendor command IDs, register offsets, or FCP operands.

#pragma once

#include <DriverKit/IOReturn.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ASFW::Audio {

enum class AudioSemanticNodeKind : uint32_t {
    Endpoint = 1,
    Router = 2,
    Mixer = 3,
    Processor = 4,
};

enum class AudioSemanticEndpointKind : uint32_t {
    None = 0,
    Physical = 1,
    Host = 2,
};

enum class AudioSemanticPortDirection : uint32_t { Input = 1, Output = 2 };

enum class AudioSemanticSignalKind : uint32_t {
    None = 0,
    AnalogMicXlr = 1,
    AnalogInstrument = 2,
    AnalogLine = 3,
    Headphone = 4,
    HostStream = 5,
};

enum class AudioSemanticTargetKind : uint32_t { Port = 1, Crosspoint = 2 };

enum class AudioSemanticParameterKind : uint32_t {
    Level = 1,
    Mute = 2,
    PhantomPower = 3,
    PhaseInvert = 4,
    NominalLevel = 5,
};

enum class AudioSemanticValueKind : uint32_t { Boolean = 1, Scalar = 2, Enum = 3 };

enum class AudioSemanticUnit : uint32_t {
    None = 0,
    Decibels = 1,
    Normalized = 2,
};

enum class AudioSemanticPresentation : uint32_t { Toggle = 1, Fader = 2, Selector = 3 };

enum class AudioSemanticMeterKind : uint32_t { Level = 1, Peak = 2 };

enum class AudioSemanticMeterUnit : uint32_t {
    Native = 0,
    Decibels = 1,
};

struct AudioSemanticNode final {
    uint32_t id{0};
    AudioSemanticNodeKind kind{AudioSemanticNodeKind::Endpoint};
    AudioSemanticEndpointKind endpointKind{AudioSemanticEndpointKind::None};
};
static_assert(sizeof(AudioSemanticNode) == 12);

struct AudioSemanticPort final {
    uint32_t id{0};
    uint32_t ownerNodeId{0};
    AudioSemanticPortDirection direction{AudioSemanticPortDirection::Input};
    AudioSemanticSignalKind signalKind{AudioSemanticSignalKind::None};
    uint32_t signalIndex{0};
};
static_assert(sizeof(AudioSemanticPort) == 20);

struct AudioSemanticFixedLink final {
    uint32_t sourcePortId{0};
    uint32_t destinationPortId{0};
};
static_assert(sizeof(AudioSemanticFixedLink) == 8);

struct AudioSemanticRouter final {
    uint32_t nodeId{0};
    uint32_t maxActiveBundles{0};
    uint32_t maxSourcesPerOutput{0};
    uint32_t maxDestinationsPerInput{0};
};
static_assert(sizeof(AudioSemanticRouter) == 16);

struct AudioSemanticRouteBundle final {
    uint32_t routerNodeId{0};
    uint32_t bundleId{0};
    uint32_t routeOffset{0};
    uint32_t routeCount{0};
};
static_assert(sizeof(AudioSemanticRouteBundle) == 16);

struct AudioSemanticRoute final {
    uint32_t sourcePortId{0};
    uint32_t destinationPortId{0};
};
static_assert(sizeof(AudioSemanticRoute) == 8);

struct AudioSemanticCrosspoint final {
    uint32_t id{0};
    uint32_t sourcePortId{0};
    uint32_t destinationPortId{0};
};
static_assert(sizeof(AudioSemanticCrosspoint) == 12);

// Scalar domains use micro-units for stable, lossless UserClient transport:
// e.g. 1.0 normalized is 1'000'000, and 0.5 is 500'000.
struct AudioSemanticParameter final {
    uint32_t id{0};
    AudioSemanticTargetKind targetKind{AudioSemanticTargetKind::Port};
    uint32_t targetId{0};
    AudioSemanticParameterKind kind{AudioSemanticParameterKind::Level};
    AudioSemanticValueKind valueKind{AudioSemanticValueKind::Scalar};
    AudioSemanticUnit unit{AudioSemanticUnit::None};
    int32_t minimum{0};
    int32_t maximum{0};
    int32_t step{0};
    AudioSemanticPresentation presentation{AudioSemanticPresentation::Fader};
};
static_assert(sizeof(AudioSemanticParameter) == 40);

struct AudioSemanticMeter final {
    uint32_t id{0};
    uint32_t targetPortId{0};
    AudioSemanticMeterKind kind{AudioSemanticMeterKind::Level};
    AudioSemanticMeterUnit unit{AudioSemanticMeterUnit::Native};
    int32_t minimum{0};
    int32_t maximum{0};
};
static_assert(sizeof(AudioSemanticMeter) == 24);

inline constexpr uint32_t kAudioSemanticTopologyVersion = 1;
inline constexpr size_t kMaxAudioSemanticNodes = 16;
inline constexpr size_t kMaxAudioSemanticPorts = 40;
inline constexpr size_t kMaxAudioSemanticFixedLinks = 40;
inline constexpr size_t kMaxAudioSemanticRouters = 8;
inline constexpr size_t kMaxAudioSemanticRouteBundles = 16;
inline constexpr size_t kMaxAudioSemanticRoutes = 24;
inline constexpr size_t kMaxAudioSemanticCrosspoints = 24;
inline constexpr size_t kMaxAudioSemanticParameters = 24;
inline constexpr size_t kMaxAudioSemanticMeters = 12;

struct AudioSemanticTopologySnapshot final {
    uint32_t version{kAudioSemanticTopologyVersion};
    uint32_t deviceKind{0};
    uint64_t topologyRevision{0};
    uint32_t nodeCount{0};
    uint32_t portCount{0};
    uint32_t fixedLinkCount{0};
    uint32_t routerCount{0};
    uint32_t routeBundleCount{0};
    uint32_t routeCount{0};
    uint32_t crosspointCount{0};
    uint32_t parameterCount{0};
    uint32_t meterCount{0};
    std::array<AudioSemanticNode, kMaxAudioSemanticNodes> nodes{};
    std::array<AudioSemanticPort, kMaxAudioSemanticPorts> ports{};
    std::array<AudioSemanticFixedLink, kMaxAudioSemanticFixedLinks> fixedLinks{};
    std::array<AudioSemanticRouter, kMaxAudioSemanticRouters> routers{};
    std::array<AudioSemanticRouteBundle, kMaxAudioSemanticRouteBundles> routeBundles{};
    std::array<AudioSemanticRoute, kMaxAudioSemanticRoutes> routes{};
    std::array<AudioSemanticCrosspoint, kMaxAudioSemanticCrosspoints> crosspoints{};
    std::array<AudioSemanticParameter, kMaxAudioSemanticParameters> parameters{};
    std::array<AudioSemanticMeter, kMaxAudioSemanticMeters> meters{};
};
// The app decodes this as a fixed UserClient ABI. Keep each offset explicit so
// changing a C++ enum, alignment rule, or array capacity cannot silently
// scramble the Swift-side topology.
static_assert(offsetof(AudioSemanticTopologySnapshot, nodes) == 52);
static_assert(offsetof(AudioSemanticTopologySnapshot, ports) == 244);
static_assert(offsetof(AudioSemanticTopologySnapshot, fixedLinks) == 1044);
static_assert(offsetof(AudioSemanticTopologySnapshot, routers) == 1364);
static_assert(offsetof(AudioSemanticTopologySnapshot, routeBundles) == 1492);
static_assert(offsetof(AudioSemanticTopologySnapshot, routes) == 1748);
static_assert(offsetof(AudioSemanticTopologySnapshot, crosspoints) == 1940);
static_assert(offsetof(AudioSemanticTopologySnapshot, parameters) == 2228);
static_assert(offsetof(AudioSemanticTopologySnapshot, meters) == 3188);
static_assert(sizeof(AudioSemanticTopologySnapshot) == 3480,
              "semantic topology ABI changed");

class IAudioSemanticTopology {
public:
    virtual ~IAudioSemanticTopology() = default;
    [[nodiscard]] virtual bool CopyAudioSemanticTopology(
        AudioSemanticTopologySnapshot& outSnapshot) const noexcept = 0;
};

} // namespace ASFW::Audio
