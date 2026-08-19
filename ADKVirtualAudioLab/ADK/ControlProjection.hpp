#pragma once

#include "../Core/AudioModel/State.hpp"
#include "../Core/AudioModel/Topology.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ASFW::ADK {

enum class ProjectedControlKind {
    Volume,
    Mute,
    Boolean,
    Enum,
};

struct ProjectedVolumeControl {
    double minValue{0.0};
    double maxValue{1.0};
    double currentValue{1.0};
};

struct ProjectedMuteControl {
    bool isMuted{false};
};

struct ProjectedBooleanControl {
    bool value{false};
};

struct ProjectedEnumItem {
    int64_t value{0};
    std::string name;
};

struct ProjectedEnumControl {
    std::vector<ProjectedEnumItem> items;
    int64_t selectedValue{0};
};

using ProjectedControlData = std::variant<
    ProjectedVolumeControl,
    ProjectedMuteControl,
    ProjectedBooleanControl,
    ProjectedEnumControl
>;

struct ProjectedControl {
    AudioModel::ParameterId parameterId;
    std::string name;
    ProjectedControlKind kind;
    ProjectedControlData data;
};

std::vector<ProjectedControl> projectControls(
    const AudioModel::Topology& topology,
    const AudioModel::DeviceState& state);

} // namespace ASFW::ADK
