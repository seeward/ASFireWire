#pragma once

#include "../AudioModel/Id.hpp"
#include <string>
#include <vector>

namespace ASFW::Presentation {

using PresentationGroupId = AudioModel::Id<struct PresentationGroupTag>;

enum class PresentationGroupKind {
    InputChannel,
    OutputChannel,
    Mixer,
    Monitor,
    Routing,
    Processor,
    Other,
};

enum class RouterPresentationStyle {
    Auto,
    Selector,
    Patchbay,
    Matrix,
};

enum class MixerPresentationStyle {
    Auto,
    ChannelStrips,
    Matrix,
};

enum class ControlPresentation {
    Auto,
    Fader,
    Rotary,
    Toggle,
    Selector,
};

enum class ControlPlacement {
    Auto,
    ChannelHeader,
    ChannelStrip,
    ChannelFooter,
    Crosspoint,
    Master,
    Advanced,
};

struct PortPresentationGroup {
    std::string name;
    std::vector<AudioModel::PortId> ports;
};

struct RouteBundleGroup {
    std::string name;
    std::vector<AudioModel::RouteBundleId> bundles;
};

struct PresentationGroup {
    PresentationGroupId id;
    std::string name;
    PresentationGroupKind kind{PresentationGroupKind::Other};

    std::vector<AudioModel::NodeId> nodes;
    std::vector<AudioModel::PortId> ports;
    std::vector<AudioModel::ParameterId> parameters;
    std::vector<AudioModel::MeterId> meters;
};

struct RouterPresentationHint {
    AudioModel::NodeId router;
    RouterPresentationStyle style{RouterPresentationStyle::Auto};

    std::vector<PortPresentationGroup> inputGroups;
    std::vector<PortPresentationGroup> outputGroups;
    std::vector<RouteBundleGroup> bundleGroups;
};

struct MixerPresentationHint {
    AudioModel::NodeId mixer;
    MixerPresentationStyle style{MixerPresentationStyle::Auto};
    std::vector<PortPresentationGroup> inputGroups;
    std::vector<PortPresentationGroup> outputGroups;
};

struct ParameterPresentationHint {
    AudioModel::ParameterId parameter;
    ControlPlacement placement{ControlPlacement::Auto};
    ControlPresentation presentation{ControlPresentation::Auto};
    std::string section;
};

struct DevicePresentation {
    std::vector<PresentationGroup> groups;
    std::vector<RouterPresentationHint> routers;
    std::vector<MixerPresentationHint> mixers;
    std::vector<ParameterPresentationHint> parameters;
};

} // namespace ASFW::Presentation
