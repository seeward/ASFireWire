#include "LabEventLog.hpp"

#include <format>

namespace ASFW::Runtime {

const char* describe(LabEventKind kind) noexcept {
    switch (kind) {
        case LabEventKind::ConfigurationCommitted: return "config";
        case LabEventKind::ConfigurationRejected:  return "config!";
        case LabEventKind::ParameterChanged:       return "param";
        case LabEventKind::ParameterRejected:      return "param!";
        case LabEventKind::RouteBundlesChanged:    return "route";
        case LabEventKind::RouteBundlesRejected:   return "route!";
    }
    return "?";
}

std::string formatEvent(const LabEvent& event) {
    std::string line = std::format("#{:<4} {:<8} rev {:<3} {}",
                                   event.sequence, describe(event.kind), event.revision, event.label);
    if (!event.before.empty() || !event.after.empty()) {
        line += std::format("  {} -> {}", event.before, event.after);
    }
    if (!event.detail.empty()) {
        line += std::format("  ({})", event.detail);
    }
    return line;
}

} // namespace ASFW::Runtime
