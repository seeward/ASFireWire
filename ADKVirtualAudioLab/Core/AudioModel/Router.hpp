#pragma once

#include "Id.hpp"

#include <compare>
#include <optional>
#include <vector>

namespace ASFW::AudioModel {

struct Route {
    PortId input;
    PortId output;

    constexpr auto operator<=>(const Route&) const = default;
};

struct RouteBundle {
    RouteBundleId id;
    std::vector<Route> routes;

    auto operator<=>(const RouteBundle&) const = default;
};

struct RouterConstraints {
    std::optional<uint32_t> maxActiveBundles;
    std::optional<uint32_t> maxActiveRoutes;
    std::optional<uint32_t> maxSourcesPerOutput;
    std::optional<uint32_t> maxDestinationsPerInput;

    constexpr auto operator<=>(const RouterConstraints&) const = default;
};

} // namespace ASFW::AudioModel
