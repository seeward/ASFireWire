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
#include <expected>

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

enum class AudioSemanticTargetKind : uint32_t { Port = 1, Crosspoint = 2, Device = 3 };

enum class AudioSemanticParameterKind : uint32_t {
    Level = 1,
    Mute = 2,
    PhantomPower = 3,
    PhaseInvert = 4,
    NominalLevel = 5,
    Source = 6,
    StereoLink = 7,
    HardwareControlTarget = 8,
    /// Defines how the device-wide mute state affects a physical output pair.
    /// The enum domain is protocol-neutral: never, mute asserted, mute released.
    MuteFollow = 9,
};

enum class AudioSemanticValueKind : uint32_t { Boolean = 1, Scalar = 2, Enum = 3 };

enum class AudioSemanticUnit : uint32_t {
    None = 0,
    Decibels = 1,
    Normalized = 2,
};

enum class AudioSemanticPresentation : uint32_t { Toggle = 1, Fader = 2, Selector = 3 };

// A crosspoint is still a complete signal-graph edge.  These presentation
// hints only describe how a console should plot that edge, so a client never
// has to infer a stereo pair (or a crossfeed) from incidental port numbering.
enum class AudioSemanticCrosspointPresentation : uint32_t {
    None = 0,
    PrimaryFader = 1,
    RoutingFader = 2,
};

enum class AudioSemanticCrosspointGroup : uint32_t {
    None = 0,
    InputMonitor = 1,
    HostPlayback = 2,
};

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
    AudioSemanticCrosspointPresentation presentation{AudioSemanticCrosspointPresentation::None};
    AudioSemanticCrosspointGroup presentationGroup{AudioSemanticCrosspointGroup::None};
    uint32_t presentationOrder{0};
};
static_assert(sizeof(AudioSemanticCrosspoint) == 24);

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

inline constexpr uint32_t kAudioSemanticTopologyVersion = 3;
inline constexpr size_t kMaxAudioSemanticTopologyEndpoints = 8;
inline constexpr size_t kMaxAudioSemanticNodes = 16;
inline constexpr size_t kMaxAudioSemanticPorts = 40;
inline constexpr size_t kMaxAudioSemanticFixedLinks = 40;
inline constexpr size_t kMaxAudioSemanticRouters = 8;
inline constexpr size_t kMaxAudioSemanticRouteBundles = 16;
inline constexpr size_t kMaxAudioSemanticRoutes = 24;
inline constexpr size_t kMaxAudioSemanticCrosspoints = 24;
inline constexpr size_t kMaxAudioSemanticParameters = 28;
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
static_assert(offsetof(AudioSemanticTopologySnapshot, parameters) == 2516);
static_assert(offsetof(AudioSemanticTopologySnapshot, meters) == 3636);
static_assert(sizeof(AudioSemanticTopologySnapshot) == 3928,
              "semantic topology ABI changed");

class IAudioSemanticTopology {
public:
    virtual ~IAudioSemanticTopology() = default;
    [[nodiscard]] virtual bool CopyAudioSemanticTopology(
        AudioSemanticTopologySnapshot& outSnapshot) const noexcept = 0;
};

/// Bounded structural validation for the driver/UI topology ABI.
///
/// This is the production counterpart of ADKVirtualAudioLab's AudioModel
/// validator.  It intentionally validates only facts represented by this
/// fixed snapshot: no allocations, strings, or runtime device state are needed
/// on a DriverKit queue.  A producer must validate a completed snapshot before
/// publishing it; a consumer may use the same routine as a defensive check in
/// host tests.
enum class AudioSemanticTopologyValidationError : uint32_t {
    InvalidVersion,
    MissingDeviceKind,
    MissingTopologyRevision,
    CountOutOfRange,
    InvalidNode,
    DuplicateNodeId,
    InvalidPort,
    DuplicatePortId,
    MissingPortOwner,
    InvalidFixedLink,
    DuplicateFixedLink,
    InvalidRouter,
    DuplicateRouter,
    InvalidRouteBundle,
    DuplicateRouteBundle,
    InvalidRoute,
    InvalidCrosspoint,
    DuplicateCrosspointId,
    DuplicateCrosspoint,
    InvalidParameter,
    DuplicateParameterId,
    InvalidMeter,
    DuplicateMeterId,
};

namespace Detail {

[[nodiscard]] constexpr bool IsValid(AudioSemanticNodeKind value) noexcept {
    return value == AudioSemanticNodeKind::Endpoint || value == AudioSemanticNodeKind::Router ||
           value == AudioSemanticNodeKind::Mixer || value == AudioSemanticNodeKind::Processor;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticEndpointKind value) noexcept {
    return value == AudioSemanticEndpointKind::None || value == AudioSemanticEndpointKind::Physical ||
           value == AudioSemanticEndpointKind::Host;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticPortDirection value) noexcept {
    return value == AudioSemanticPortDirection::Input || value == AudioSemanticPortDirection::Output;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticSignalKind value) noexcept {
    return value == AudioSemanticSignalKind::None || value == AudioSemanticSignalKind::AnalogMicXlr ||
           value == AudioSemanticSignalKind::AnalogInstrument || value == AudioSemanticSignalKind::AnalogLine ||
           value == AudioSemanticSignalKind::Headphone || value == AudioSemanticSignalKind::HostStream;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticTargetKind value) noexcept {
    return value == AudioSemanticTargetKind::Port || value == AudioSemanticTargetKind::Crosspoint ||
           value == AudioSemanticTargetKind::Device;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticParameterKind value) noexcept {
    return value == AudioSemanticParameterKind::Level || value == AudioSemanticParameterKind::Mute ||
           value == AudioSemanticParameterKind::PhantomPower || value == AudioSemanticParameterKind::PhaseInvert ||
           value == AudioSemanticParameterKind::NominalLevel || value == AudioSemanticParameterKind::Source ||
           value == AudioSemanticParameterKind::StereoLink ||
           value == AudioSemanticParameterKind::HardwareControlTarget ||
           value == AudioSemanticParameterKind::MuteFollow;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticValueKind value) noexcept {
    return value == AudioSemanticValueKind::Boolean || value == AudioSemanticValueKind::Scalar ||
           value == AudioSemanticValueKind::Enum;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticUnit value) noexcept {
    return value == AudioSemanticUnit::None || value == AudioSemanticUnit::Decibels ||
           value == AudioSemanticUnit::Normalized;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticPresentation value) noexcept {
    return value == AudioSemanticPresentation::Toggle || value == AudioSemanticPresentation::Fader ||
           value == AudioSemanticPresentation::Selector;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticCrosspointPresentation value) noexcept {
    return value == AudioSemanticCrosspointPresentation::None ||
           value == AudioSemanticCrosspointPresentation::PrimaryFader ||
           value == AudioSemanticCrosspointPresentation::RoutingFader;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticCrosspointGroup value) noexcept {
    return value == AudioSemanticCrosspointGroup::None ||
           value == AudioSemanticCrosspointGroup::InputMonitor ||
           value == AudioSemanticCrosspointGroup::HostPlayback;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticMeterKind value) noexcept {
    return value == AudioSemanticMeterKind::Level || value == AudioSemanticMeterKind::Peak;
}

[[nodiscard]] constexpr bool IsValid(AudioSemanticMeterUnit value) noexcept {
    return value == AudioSemanticMeterUnit::Native || value == AudioSemanticMeterUnit::Decibels;
}

[[nodiscard]] constexpr const AudioSemanticNode* FindNode(
    const AudioSemanticTopologySnapshot& snapshot, uint32_t id) noexcept {
    for (uint32_t index = 0; index < snapshot.nodeCount; ++index) {
        if (snapshot.nodes[index].id == id) return &snapshot.nodes[index];
    }
    return nullptr;
}

[[nodiscard]] constexpr const AudioSemanticPort* FindPort(
    const AudioSemanticTopologySnapshot& snapshot, uint32_t id) noexcept {
    for (uint32_t index = 0; index < snapshot.portCount; ++index) {
        if (snapshot.ports[index].id == id) return &snapshot.ports[index];
    }
    return nullptr;
}

[[nodiscard]] constexpr const AudioSemanticCrosspoint* FindCrosspoint(
    const AudioSemanticTopologySnapshot& snapshot, uint32_t id) noexcept {
    for (uint32_t index = 0; index < snapshot.crosspointCount; ++index) {
        if (snapshot.crosspoints[index].id == id) return &snapshot.crosspoints[index];
    }
    return nullptr;
}

} // namespace Detail

[[nodiscard]] constexpr std::expected<void, AudioSemanticTopologyValidationError>
ValidateAudioSemanticTopology(const AudioSemanticTopologySnapshot& snapshot) noexcept {
    using Error = AudioSemanticTopologyValidationError;
    if (snapshot.version != kAudioSemanticTopologyVersion) return std::unexpected(Error::InvalidVersion);
    if (snapshot.deviceKind == 0) return std::unexpected(Error::MissingDeviceKind);
    if (snapshot.topologyRevision == 0) return std::unexpected(Error::MissingTopologyRevision);
    if (snapshot.nodeCount > kMaxAudioSemanticNodes || snapshot.portCount > kMaxAudioSemanticPorts ||
        snapshot.fixedLinkCount > kMaxAudioSemanticFixedLinks ||
        snapshot.routerCount > kMaxAudioSemanticRouters ||
        snapshot.routeBundleCount > kMaxAudioSemanticRouteBundles ||
        snapshot.routeCount > kMaxAudioSemanticRoutes ||
        snapshot.crosspointCount > kMaxAudioSemanticCrosspoints ||
        snapshot.parameterCount > kMaxAudioSemanticParameters ||
        snapshot.meterCount > kMaxAudioSemanticMeters) {
        return std::unexpected(Error::CountOutOfRange);
    }

    for (uint32_t index = 0; index < snapshot.nodeCount; ++index) {
        const auto& node = snapshot.nodes[index];
        if (node.id == 0 || !Detail::IsValid(node.kind) || !Detail::IsValid(node.endpointKind) ||
            (node.kind == AudioSemanticNodeKind::Endpoint) !=
                (node.endpointKind != AudioSemanticEndpointKind::None)) {
            return std::unexpected(Error::InvalidNode);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.nodes[earlier].id == node.id) return std::unexpected(Error::DuplicateNodeId);
        }
    }

    for (uint32_t index = 0; index < snapshot.portCount; ++index) {
        const auto& port = snapshot.ports[index];
        const auto* owner = Detail::FindNode(snapshot, port.ownerNodeId);
        if (port.id == 0 || !Detail::IsValid(port.direction) || !Detail::IsValid(port.signalKind)) {
            return std::unexpected(Error::InvalidPort);
        }
        if (!owner) return std::unexpected(Error::MissingPortOwner);
        if (owner->kind == AudioSemanticNodeKind::Endpoint &&
            (port.signalKind == AudioSemanticSignalKind::None || port.signalIndex == 0)) {
            return std::unexpected(Error::InvalidPort);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.ports[earlier].id == port.id) return std::unexpected(Error::DuplicatePortId);
        }
    }

    for (uint32_t index = 0; index < snapshot.fixedLinkCount; ++index) {
        const auto& link = snapshot.fixedLinks[index];
        const auto* source = Detail::FindPort(snapshot, link.sourcePortId);
        const auto* destination = Detail::FindPort(snapshot, link.destinationPortId);
        if (!source || !destination || source == destination ||
            source->direction != AudioSemanticPortDirection::Output ||
            destination->direction != AudioSemanticPortDirection::Input) {
            return std::unexpected(Error::InvalidFixedLink);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            const auto& previous = snapshot.fixedLinks[earlier];
            if (previous.sourcePortId == link.sourcePortId &&
                previous.destinationPortId == link.destinationPortId) {
                return std::unexpected(Error::DuplicateFixedLink);
            }
        }
    }

    for (uint32_t index = 0; index < snapshot.routerCount; ++index) {
        const auto& router = snapshot.routers[index];
        const auto* node = Detail::FindNode(snapshot, router.nodeId);
        if (!node || node->kind != AudioSemanticNodeKind::Router ||
            router.maxActiveBundles == 0 || router.maxSourcesPerOutput == 0 ||
            router.maxDestinationsPerInput == 0) {
            return std::unexpected(Error::InvalidRouter);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.routers[earlier].nodeId == router.nodeId) {
                return std::unexpected(Error::DuplicateRouter);
            }
        }
    }

    for (uint32_t index = 0; index < snapshot.routeBundleCount; ++index) {
        const auto& bundle = snapshot.routeBundles[index];
        bool routerExists = false;
        for (uint32_t routerIndex = 0; routerIndex < snapshot.routerCount; ++routerIndex) {
            routerExists = routerExists || snapshot.routers[routerIndex].nodeId == bundle.routerNodeId;
        }
        if (!routerExists || bundle.bundleId == 0 || bundle.routeCount == 0 ||
            bundle.routeOffset > snapshot.routeCount ||
            bundle.routeCount > snapshot.routeCount - bundle.routeOffset) {
            return std::unexpected(Error::InvalidRouteBundle);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            const auto& previous = snapshot.routeBundles[earlier];
            if (previous.routerNodeId == bundle.routerNodeId && previous.bundleId == bundle.bundleId) {
                return std::unexpected(Error::DuplicateRouteBundle);
            }
        }
    }

    for (uint32_t index = 0; index < snapshot.routeCount; ++index) {
        const auto& route = snapshot.routes[index];
        const auto* source = Detail::FindPort(snapshot, route.sourcePortId);
        const auto* destination = Detail::FindPort(snapshot, route.destinationPortId);
        if (!source || !destination || source == destination ||
            source->direction != AudioSemanticPortDirection::Input ||
            destination->direction != AudioSemanticPortDirection::Output ||
            source->ownerNodeId != destination->ownerNodeId ||
            Detail::FindNode(snapshot, source->ownerNodeId)->kind != AudioSemanticNodeKind::Router) {
            return std::unexpected(Error::InvalidRoute);
        }
    }

    for (uint32_t index = 0; index < snapshot.crosspointCount; ++index) {
        const auto& crosspoint = snapshot.crosspoints[index];
        const auto* source = Detail::FindPort(snapshot, crosspoint.sourcePortId);
        const auto* destination = Detail::FindPort(snapshot, crosspoint.destinationPortId);
        if (crosspoint.id == 0 || !source || !destination || source == destination ||
            source->direction != AudioSemanticPortDirection::Input ||
            destination->direction != AudioSemanticPortDirection::Output ||
            source->ownerNodeId != destination->ownerNodeId ||
            Detail::FindNode(snapshot, source->ownerNodeId)->kind != AudioSemanticNodeKind::Mixer ||
            !Detail::IsValid(crosspoint.presentation) || !Detail::IsValid(crosspoint.presentationGroup)) {
            return std::unexpected(Error::InvalidCrosspoint);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            const auto& previous = snapshot.crosspoints[earlier];
            if (previous.id == crosspoint.id) return std::unexpected(Error::DuplicateCrosspointId);
            if (previous.sourcePortId == crosspoint.sourcePortId &&
                previous.destinationPortId == crosspoint.destinationPortId) {
                return std::unexpected(Error::DuplicateCrosspoint);
            }
        }
    }

    for (uint32_t index = 0; index < snapshot.parameterCount; ++index) {
        const auto& parameter = snapshot.parameters[index];
        bool targetExists = parameter.targetKind == AudioSemanticTargetKind::Device && parameter.targetId != 0;
        if (parameter.targetKind == AudioSemanticTargetKind::Port) {
            targetExists = Detail::FindPort(snapshot, parameter.targetId) != nullptr;
        } else if (parameter.targetKind == AudioSemanticTargetKind::Crosspoint) {
            targetExists = Detail::FindCrosspoint(snapshot, parameter.targetId) != nullptr;
        }
        const bool presentationMatchesValue =
            (parameter.valueKind == AudioSemanticValueKind::Boolean &&
             parameter.presentation == AudioSemanticPresentation::Toggle && parameter.unit == AudioSemanticUnit::None &&
             parameter.minimum == 0 && parameter.maximum == 1 && parameter.step == 1) ||
            (parameter.valueKind == AudioSemanticValueKind::Scalar &&
             parameter.presentation == AudioSemanticPresentation::Fader) ||
            (parameter.valueKind == AudioSemanticValueKind::Enum &&
             parameter.presentation == AudioSemanticPresentation::Selector && parameter.unit == AudioSemanticUnit::None);
        if (parameter.id == 0 || !targetExists || !Detail::IsValid(parameter.targetKind) ||
            !Detail::IsValid(parameter.kind) || !Detail::IsValid(parameter.valueKind) ||
            !Detail::IsValid(parameter.unit) || !Detail::IsValid(parameter.presentation) ||
            parameter.minimum > parameter.maximum || parameter.step <= 0 || !presentationMatchesValue) {
            return std::unexpected(Error::InvalidParameter);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.parameters[earlier].id == parameter.id) {
                return std::unexpected(Error::DuplicateParameterId);
            }
        }
    }

    for (uint32_t index = 0; index < snapshot.meterCount; ++index) {
        const auto& meter = snapshot.meters[index];
        if (meter.id == 0 || !Detail::FindPort(snapshot, meter.targetPortId) ||
            !Detail::IsValid(meter.kind) || !Detail::IsValid(meter.unit) ||
            meter.minimum > meter.maximum) {
            return std::unexpected(Error::InvalidMeter);
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier) {
            if (snapshot.meters[earlier].id == meter.id) {
                return std::unexpected(Error::DuplicateMeterId);
            }
        }
    }

    return {};
}

} // namespace ASFW::Audio
