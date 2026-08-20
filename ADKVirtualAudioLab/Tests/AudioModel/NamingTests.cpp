#include "../TestHarness.hpp"
#include "../../Core/AudioModel/Naming.hpp"
#include "../../Core/AudioModel/Validate.hpp"
#include "../../Devices/Duet/Resolve.hpp"
#include "../../Devices/Phase88/Resolve.hpp"
#include "../../Devices/FW1814/Resolve.hpp"
#include "../../Devices/SaffirePro24DSP/Resolve.hpp"

#include <string>

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

namespace {

const Port* portById(const Topology& t, uint32_t id) {
    for (const auto& p : t.ports) {
        if (p.id.value == id) return &p;
    }
    return nullptr;
}

void checkName(TestContext& ctx, const Topology& t, uint32_t portId, const char* expected) {
    const Port* port = portById(t, portId);
    REQUIRE(ctx, port != nullptr);
    const std::string actual = displayName(*port);
    ++ctx.checks;
    if (actual != expected) {
        ++ctx.failures;
        std::printf("FAIL %s:%d  port %u name '%s', expected '%s'\n",
                    __FILE__, __LINE__, portId, actual.c_str(), expected);
    }
}

void checkLabel(TestContext& ctx, const Topology& t, uint32_t portId, const char* expected) {
    auto label = sourceLabel(t, PortId{portId});
    REQUIRE(ctx, label.has_value());
    ++ctx.checks;
    if (*label != expected) {
        ++ctx.failures;
        std::printf("FAIL %s:%d  source of port %u is '%s', expected '%s'\n",
                    __FILE__, __LINE__, portId, label->c_str(), expected);
    }
}

// Every endpoint port must render the same way for the same connector, so a
// device may never contribute a spelling of its own.
void checkEveryEndpointIsCanonical(TestContext& ctx, const Topology& t) {
    for (const auto& node : t.nodes) {
        if (!std::holds_alternative<EndpointNode>(node.body)) continue;
        for (const auto& port : t.ports) {
            if (port.owner != node.id) continue;
            CHECK(ctx, port.signal.kind != SignalKind::Unknown);
            CHECK(ctx, port.signal.index != 0);
            CHECK(ctx, port.name.empty());
            CHECK(ctx, displayName(port) ==
                       canonicalName(port.signal, port.direction, port.channels));
        }
    }
}

} // namespace

void RunNamingTests(TestContext& ctx) {
    // Rendering rules, independent of any device.
    CHECK(ctx, canonicalName({SignalKind::Adat, 3}, PortDirection::Output, 2) == "ADAT In 3/4");
    CHECK(ctx, canonicalName({SignalKind::Adat, 3}, PortDirection::Input, 1) == "ADAT Out 3");
    CHECK(ctx, canonicalName({SignalKind::AnalogMicXlr, 1}, PortDirection::Output, 1) == "XLR In 1");
    CHECK(ctx, canonicalName({SignalKind::SpdifCoaxial, 1}, PortDirection::Input, 2) == "S/PDIF Out 1/2");
    CHECK(ctx, canonicalName({SignalKind::HostStream, 5}, PortDirection::Output, 2) == "Playback 5/6");
    CHECK(ctx, canonicalName({SignalKind::HostStream, 5}, PortDirection::Input, 1) == "Capture 5");
    CHECK(ctx, canonicalName({SignalKind::Adat, 1}, PortDirection::Output, 8) == "ADAT In 1-8");

    {
        auto resolved = Devices::Duet::resolve(DeviceConfiguration{.sampleRate = 48000});
        REQUIRE(ctx, resolved.has_value());
        const auto& t = resolved->topology;
        CHECK(ctx, validate(t).has_value());
        checkEveryEndpointIsCanonical(ctx, t);

        checkName(ctx, t, 1, "XLR In 1");
        checkName(ctx, t, 3, "Inst In 1");
        checkName(ctx, t, 33, "Playback 1");
        checkName(ctx, t, 63, "Headphone Out 1");

        // The input selector's alternatives are two different connectors.
        checkLabel(ctx, t, 11, "XLR In 1");
        checkLabel(ctx, t, 12, "Inst In 1");
        // The output selector's mixer leg is a bus, not a connector.
        checkLabel(ctx, t, 53, "Mixer Master L/R");
    }

    {
        auto resolved = Devices::Phase88::resolve(DeviceConfiguration{.sampleRate = 48000});
        REQUIRE(ctx, resolved.has_value());
        const auto& t = resolved->topology;
        CHECK(ctx, validate(t).has_value());
        checkEveryEndpointIsCanonical(ctx, t);

        checkName(ctx, t, 1, "Line In 1");
        checkName(ctx, t, 9, "S/PDIF In 1");
        checkName(ctx, t, 120, "S/PDIF Out 2");
    }

    {
        const DeviceConfiguration adat{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Adat,
            .opticalOutput = OpticalMode::Adat,
        };
        auto resolved = Devices::FW1814::resolve(adat);
        REQUIRE(ctx, resolved.has_value());
        const auto& t = resolved->topology;
        CHECK(ctx, validate(t).has_value());
        checkEveryEndpointIsCanonical(ctx, t);

        checkName(ctx, t, 1, "Line In 1/2");
        checkName(ctx, t, 11, "ADAT In 1/2");
        checkName(ctx, t, 143, "Headphone Out 1/2");

        // Regression: these three feed the headphone selector and used to
        // render as two identical "Digital Master Mix L/R" pills plus one
        // correct entry, because the label was sniffed from port-name text.
        checkLabel(ctx, t, 131, "Mixer 1");
        checkLabel(ctx, t, 132, "Mixer 2");
        checkLabel(ctx, t, 133, "Aux");
    }

    {
        const DeviceConfiguration adat{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Adat,
            .opticalOutput = OpticalMode::Adat,
        };
        auto resolved = Devices::SaffirePro24DSP::resolve(adat);
        REQUIRE(ctx, resolved.has_value());
        const auto& t = resolved->topology;
        CHECK(ctx, validate(t).has_value());
        checkEveryEndpointIsCanonical(ctx, t);

        checkName(ctx, t, 9, "ADAT In 1");
        checkName(ctx, t, 241, "S/PDIF Out 1");

        // Optical mode changes what the connector *is*, not just its label.
        const DeviceConfiguration spdif{
            .sampleRate = 48000,
            .opticalInput = OpticalMode::Spdif,
            .opticalOutput = OpticalMode::Spdif,
        };
        auto optical = Devices::SaffirePro24DSP::resolve(spdif);
        REQUIRE(ctx, optical.has_value());
        checkName(ctx, optical->topology, 9, "Opt S/PDIF In 1");
        CHECK(ctx, portById(optical->topology, 11) == nullptr);
    }

    // A missing identity on an endpoint port is a validation error.
    {
        auto resolved = Devices::Duet::resolve(DeviceConfiguration{.sampleRate = 48000});
        REQUIRE(ctx, resolved.has_value());
        auto broken = resolved->topology;
        for (auto& port : broken.ports) {
            if (port.id.value == 1) port.signal = SignalIdentity{};
        }
        auto result = validate(broken);
        CHECK(ctx, !result.has_value());
        if (!result.has_value()) {
            CHECK(ctx, result.error().kind == TopologyErrorKind::MissingSignalIdentity);
        }
    }
}

} // namespace ASFW::LabTests
