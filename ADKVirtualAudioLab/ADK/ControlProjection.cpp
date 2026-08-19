#include "ControlProjection.hpp"

namespace ASFW::ADK {

using namespace ASFW::AudioModel;

std::vector<ProjectedControl> projectControls(
    const Topology& topology,
    const DeviceState& state) {

    std::vector<ProjectedControl> projectedList;
    projectedList.reserve(topology.parameters.size());

    for (const auto& param : topology.parameters) {
        ProjectedControl ctrl;
        ctrl.parameterId = param.id;
        ctrl.name = param.name;

        auto it = state.parameters.find(param.id);

        std::visit([&](const auto& domain) {
            using D = std::decay_t<decltype(domain)>;
            if constexpr (std::is_same_v<D, BooleanDomain>) {
                bool val = false;
                if (it != state.parameters.end() && std::holds_alternative<bool>(it->second)) {
                    val = std::get<bool>(it->second);
                }

                if (param.semantic == ParameterSemantic::Mute) {
                    ctrl.kind = ProjectedControlKind::Mute;
                    ctrl.data = ProjectedMuteControl{.isMuted = val};
                } else {
                    ctrl.kind = ProjectedControlKind::Boolean;
                    ctrl.data = ProjectedBooleanControl{.value = val};
                }
            } else if constexpr (std::is_same_v<D, ScalarDomain>) {
                double val = domain.min;
                if (it != state.parameters.end() && std::holds_alternative<double>(it->second)) {
                    val = std::get<double>(it->second);
                }

                ctrl.kind = ProjectedControlKind::Volume;
                ctrl.data = ProjectedVolumeControl{
                    .minValue = domain.min,
                    .maxValue = domain.max,
                    .currentValue = val,
                };
            } else if constexpr (std::is_same_v<D, EnumDomain>) {
                int64_t val = domain.values.empty() ? 0 : domain.values.front().value;
                if (it != state.parameters.end() && std::holds_alternative<int64_t>(it->second)) {
                    val = std::get<int64_t>(it->second);
                }

                ProjectedEnumControl enumCtrl;
                enumCtrl.selectedValue = val;
                for (const auto& item : domain.values) {
                    enumCtrl.items.push_back(ProjectedEnumItem{
                        .value = item.value,
                        .name = item.name,
                    });
                }
                ctrl.kind = ProjectedControlKind::Enum;
                ctrl.data = std::move(enumCtrl);
            }
        }, param.domain);

        projectedList.push_back(std::move(ctrl));
    }

    return projectedList;
}

} // namespace ASFW::ADK
