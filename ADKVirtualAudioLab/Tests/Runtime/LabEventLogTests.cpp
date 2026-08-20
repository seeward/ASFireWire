#include "../TestHarness.hpp"
#include "../../Runtime/VirtualDeviceRuntime.hpp"

#include <string>

namespace ASFW::LabTests {

using namespace ASFW::Runtime;
using namespace ASFW::AudioModel;
using namespace ASFW::Device;

namespace {

const LabEvent* eventAt(const VirtualDeviceRuntime& rt, size_t index) {
    const auto& events = rt.eventLog().events();
    if (index >= events.size()) return nullptr;
    return &events[index];
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

void RunLabEventLogTests(TestContext& ctx) {
    auto rtRes = VirtualDeviceRuntime::create(VirtualDeviceKind::FW1814);
    REQUIRE(ctx, rtRes.has_value());
    auto& rt = *rtRes;

    // Creation is not a mutation, so nothing is recorded yet.
    CHECK(ctx, rt.eventLog().events().empty());

    // 1. A configuration change records the whole cascade, both sides.
    {
        auto applied = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 96000,
            .opticalInput = OpticalMode::Adat,
            .opticalOutput = OpticalMode::Adat,
        });
        CHECK(ctx, applied.has_value());

        const LabEvent* event = eventAt(rt, 0);
        REQUIRE(ctx, event != nullptr);
        CHECK(ctx, event->kind == LabEventKind::ConfigurationCommitted);
        CHECK(ctx, event->accepted);
        CHECK(ctx, event->revision == rt.revision());
        // S/MUX halves ADAT, so both the stream plan and the port count move.
        CHECK(ctx, contains(event->before, "16in/12out"));
        CHECK(ctx, contains(event->after, "12in/8out"));
        CHECK(ctx, contains(event->before, "48000 Hz"));
        CHECK(ctx, contains(event->after, "96000 Hz"));
    }

    // 2. Switching the optical input to S/PDIF adds the connector selector, so
    // the node count moves too.
    {
        auto applied = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Spdif,
            .opticalOutput = OpticalMode::Adat,
        });
        CHECK(ctx, applied.has_value());

        const LabEvent* event = eventAt(rt, 1);
        REQUIRE(ctx, event != nullptr);
        CHECK(ctx, contains(event->before, "ADAT/ADAT"));
        CHECK(ctx, contains(event->after, "SPDIF/ADAT"));
        CHECK(ctx, contains(event->before, "7n"));
        CHECK(ctx, contains(event->after, "8n"));
    }

    const uint64_t revisionBeforeRejection = rt.revision();

    // 3. A refused configuration is recorded, carries the reason, and leaves
    // the revision alone.
    {
        auto rejected = rt.setConfiguration(DeviceConfiguration{
            .sampleRate = 192000,
            .opticalInput = OpticalMode::Adat,
            .opticalOutput = OpticalMode::Adat,
        });
        CHECK(ctx, !rejected.has_value());

        const LabEvent* event = eventAt(rt, 2);
        REQUIRE(ctx, event != nullptr);
        CHECK(ctx, event->kind == LabEventKind::ConfigurationRejected);
        CHECK(ctx, !event->accepted);
        CHECK(ctx, event->revision == revisionBeforeRejection);
        CHECK(ctx, event->before == event->after);
        CHECK(ctx, contains(event->detail, "192000"));
    }

    const size_t afterConfigs = rt.eventLog().events().size();

    // 4. Parameter changes name the control and render enum values as the item
    // the user picked, not the raw wire number.
    ParameterId clockId{};
    ParameterId levelId{};
    for (const auto& parameter : rt.resolved().topology.parameters) {
        if (parameter.semantic == ParameterSemantic::ClockSource) clockId = parameter.id;
        if (parameter.semantic == ParameterSemantic::Level && levelId.value == 0) levelId = parameter.id;
    }
    REQUIRE(ctx, clockId.value != 0);
    REQUIRE(ctx, levelId.value != 0);

    {
        CHECK(ctx, rt.setParameter(clockId, int64_t{1}).has_value());
        const LabEvent* event = eventAt(rt, afterConfigs);
        REQUIRE(ctx, event != nullptr);
        CHECK(ctx, event->kind == LabEventKind::ParameterChanged);
        CHECK(ctx, event->targetId == clockId.value);
        CHECK(ctx, event->label == "Clock Source");
        CHECK(ctx, contains(event->before, "Internal"));
        CHECK(ctx, contains(event->after, "Digital"));
    }

    // 5. Writing the same value again records nothing: the log tracks changes,
    // not calls.
    {
        const size_t before = rt.eventLog().events().size();
        CHECK(ctx, rt.setParameter(clockId, int64_t{1}).has_value());
        CHECK_EQ_U32(ctx, rt.eventLog().events().size(), before);
    }

    // 6. An out-of-domain value is recorded as refused, with the validator's
    // own message.
    {
        const size_t index = rt.eventLog().events().size();
        CHECK(ctx, !rt.setParameter(levelId, 99.0).has_value());

        const LabEvent* event = eventAt(rt, index);
        REQUIRE(ctx, event != nullptr);
        CHECK(ctx, event->kind == LabEventKind::ParameterRejected);
        CHECK(ctx, !event->accepted);
        CHECK(ctx, contains(event->detail, "out of range"));
    }

    // 7. Route changes record the active bundle set on both sides, and a
    // constraint violation is refused rather than silently clamped.
    {
        NodeId headphoneMux{};
        for (const auto& node : rt.resolved().topology.nodes) {
            if (node.name == "Headphone Pair Source") headphoneMux = node.id;
        }
        REQUIRE(ctx, headphoneMux.value != 0);

        const size_t index = rt.eventLog().events().size();
        const RouteBundleId reroute[] = {RouteBundleId{3}, RouteBundleId{5}};
        CHECK(ctx, rt.setActiveRouteBundles(headphoneMux, reroute).has_value());

        const LabEvent* changed = eventAt(rt, index);
        REQUIRE(ctx, changed != nullptr);
        CHECK(ctx, changed->kind == LabEventKind::RouteBundlesChanged);
        CHECK(ctx, changed->before == "1,5");
        CHECK(ctx, changed->after == "3,5");

        const RouteBundleId tooMany[] = {RouteBundleId{1}, RouteBundleId{2}, RouteBundleId{3}};
        CHECK(ctx, !rt.setActiveRouteBundles(headphoneMux, tooMany).has_value());

        const LabEvent* refused = eventAt(rt, index + 1);
        REQUIRE(ctx, refused != nullptr);
        CHECK(ctx, refused->kind == LabEventKind::RouteBundlesRejected);
        CHECK(ctx, contains(refused->detail, "maxActiveBundles"));
        // The refused set is still visible, which is the point of logging it.
        CHECK(ctx, refused->after == "1,2,3");
    }

    // 8. Sequence numbers are dense and increasing, and formatting never throws
    // away the identifying fields.
    {
        uint64_t previous = 0;
        for (const auto& event : rt.eventLog().events()) {
            CHECK(ctx, event.sequence > previous);
            previous = event.sequence;
            const std::string line = formatEvent(event);
            CHECK(ctx, contains(line, event.label.c_str()));
        }
    }

    // 9. Clearing empties the history without disturbing the model.
    {
        const uint64_t revision = rt.revision();
        rt.eventLog().clear();
        CHECK(ctx, rt.eventLog().events().empty());
        CHECK(ctx, rt.revision() == revision);
    }
}

} // namespace ASFW::LabTests
