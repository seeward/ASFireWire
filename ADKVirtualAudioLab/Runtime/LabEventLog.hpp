#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace ASFW::Runtime {

enum class LabEventKind {
    ConfigurationCommitted,
    ConfigurationRejected,
    ParameterChanged,
    ParameterRejected,
    RouteBundlesChanged,
    RouteBundlesRejected,
};

/// One recorded interaction with the device model.
///
/// Deliberately flat and string-rendered: the log is read by humans and by
/// tests, and both want to see what a control did without re-deriving it from
/// the model. `before` and `after` hold whatever the event changed -- a
/// parameter's value, a router's active bundle set, or the shape of the whole
/// resolved configuration.
struct LabEvent {
    uint64_t sequence{};
    LabEventKind kind{};
    bool accepted{true};

    /// Revision in force after the event. A rejected event leaves it unchanged.
    uint64_t revision{};

    /// ParameterId or NodeId, 0 where neither applies.
    uint32_t targetId{};

    std::string label;
    std::string before;
    std::string after;

    /// Rejection reason, or extra context for an accepted event.
    std::string detail;
};

/// Bounded, in-order history of model mutations.
class LabEventLog {
public:
    static constexpr std::size_t kCapacity = 512;

    void record(LabEvent event) {
        event.sequence = nextSequence_++;
        events_.push_back(std::move(event));
        if (events_.size() > kCapacity) {
            events_.pop_front();
        }
    }

    const std::deque<LabEvent>& events() const noexcept { return events_; }
    uint64_t recordedCount() const noexcept { return nextSequence_; }
    void clear() noexcept { events_.clear(); }

private:
    std::deque<LabEvent> events_;
    uint64_t nextSequence_{1};
};

const char* describe(LabEventKind kind) noexcept;

/// One line per event, e.g.
///   #7  config    rev 2  48000 Hz ADAT/ADAT 16in/12out 63p -> 96000 Hz ...
std::string formatEvent(const LabEvent& event);

} // namespace ASFW::Runtime
