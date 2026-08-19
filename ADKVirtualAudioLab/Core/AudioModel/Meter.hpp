#pragma once

#include "Id.hpp"
#include "Parameter.hpp"

#include <string>
#include <variant>

namespace ASFW::AudioModel {

enum class MeterSemantic {
    Level,
    Peak,
    Unknown,
};

/// A Meter represents a single scalar telemetry point (yielding 1 double in DeviceState).
/// Multi-channel metering is modeled as multiple individual Meter objects.
struct Meter {
    MeterId id;

    std::variant<NodeId, PortId> target;

    MeterSemantic semantic;
    ScalarDomain domain;

    std::string name;
};

} // namespace ASFW::AudioModel
