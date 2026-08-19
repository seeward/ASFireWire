#pragma once

#include "Id.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ASFW::AudioModel {

enum class ParameterSemantic {
    Unknown,

    Level,
    Mute,
    PhantomPower,
    PhaseInvert,
    Pan,
    Balance,
    Solo,
    NominalLevel,
    ClockSource,
    Dim,
};

using ParameterValue =
    std::variant<
        bool,
        double,
        int64_t
    >;

struct BooleanDomain {
    constexpr auto operator<=>(const BooleanDomain&) const = default;
};

enum class ScalarUnit {
    Generic,
    Decibels,
    Hertz,
    Percent,
    Normalized,
};

struct ScalarDomain {
    double min{};
    double max{};
    std::optional<double> step{};
    ScalarUnit unit{ScalarUnit::Generic};

    constexpr auto operator<=>(const ScalarDomain&) const = default;
};

struct EnumItem {
    int64_t value{};
    std::string name{};

    auto operator<=>(const EnumItem&) const = default;
};

struct EnumDomain {
    std::vector<EnumItem> values{};

    auto operator<=>(const EnumDomain&) const = default;
};

using ParameterDomain =
    std::variant<
        BooleanDomain,
        ScalarDomain,
        EnumDomain
    >;

using ParameterTarget =
    std::variant<
        NodeId,
        PortId,
        CrosspointId
    >;

struct Parameter {
    ParameterId id;
    ParameterTarget target;

    ParameterSemantic semantic;
    ParameterDomain domain;

    std::string name;
};

} // namespace ASFW::AudioModel
