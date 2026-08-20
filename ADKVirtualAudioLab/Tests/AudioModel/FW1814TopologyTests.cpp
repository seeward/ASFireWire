#include "../TestHarness.hpp"
#include "../../Devices/FW1814/Resolve.hpp"
#include "../../Core/AudioModel/Naming.hpp"
#include "../../Core/AudioModel/Validate.hpp"

#include <array>

namespace ASFW::LabTests {

using namespace ASFW::AudioModel;
using namespace ASFW::Device;

namespace {

struct ExpectedGeometry {
    uint32_t capturePcm;
    uint32_t playbackPcm;
    uint32_t mixerPairs;
};

// Linux bebob_maudio.c:228-241, decomposed: capture = 8 analog + digital_in,
// playback = 4 analog + digital_out, ADAT halving above 48 kHz under S/MUX.
ExpectedGeometry expectedFor(uint32_t rate, OpticalMode in, OpticalMode out) {
    const bool smux = rate > 48000;
    const uint32_t digIn = (in == OpticalMode::Adat) ? (smux ? 2u : 4u) : 1u;
    const uint32_t digOut = (out == OpticalMode::Adat) ? (smux ? 2u : 4u) : 1u;
    return ExpectedGeometry{
        .capturePcm = (4 + digIn) * 2,
        .playbackPcm = (2 + digOut) * 2,
        .mixerPairs = 4 + digIn + 2,
    };
}

const MixerNode* mixerNamed(const Topology& t, const char* name) {
    for (const auto& node : t.nodes) {
        if (node.name == name) return std::get_if<MixerNode>(&node.body);
    }
    return nullptr;
}

bool hasPort(const Topology& t, uint32_t id) {
    for (const auto& port : t.ports) {
        if (port.id.value == id) return true;
    }
    return false;
}

} // namespace

void RunFW1814TopologyTests(TestContext& ctx) {
    constexpr std::array<uint32_t, 4> kRates{44100, 48000, 88200, 96000};
    constexpr std::array<OpticalMode, 2> kModes{OpticalMode::Adat, OpticalMode::Spdif};

    // Exhaustive sweep: every rate against every optical combination. Device
    // modes are not orthogonal, so a defect can live in one corner only.
    for (uint32_t rate : kRates) {
        for (OpticalMode in : kModes) {
            for (OpticalMode out : kModes) {
                auto resolved = Devices::FW1814::resolve(DeviceConfiguration{
                    .sampleRate = rate,
                    .opticalInput = in,
                    .opticalOutput = out,
                });
                REQUIRE(ctx, resolved.has_value());

                const auto& t = resolved->topology;
                const auto expected = expectedFor(rate, in, out);

                auto structural = validate(t);
                CHECK(ctx, structural.has_value());
                if (!structural.has_value()) {
                    std::printf("  rate=%u in=%d out=%d: %s\n", rate, static_cast<int>(in),
                                static_cast<int>(out), structural.error().message.c_str());
                }

                CHECK_EQ_U32(ctx, resolved->streams.streams[0].channels, expected.capturePcm);
                CHECK_EQ_U32(ctx, resolved->streams.streams[1].channels, expected.playbackPcm);

                // Two stereo mixer outputs fed from the same input pairs, plus a
                // separate aux mixer over those inputs -- the 22x4 / 22x2 shape
                // of AUAA 44.3, sized to the pairs this mode actually has.
                const MixerNode* main = mixerNamed(t, "Main Mixer");
                const MixerNode* aux = mixerNamed(t, "Aux Mixer");
                REQUIRE(ctx, main != nullptr);
                REQUIRE(ctx, aux != nullptr);
                CHECK_EQ_U32(ctx, main->inputs.size(), expected.mixerPairs);
                CHECK_EQ_U32(ctx, main->outputs.size(), 2);
                CHECK_EQ_U32(ctx, main->crosspoints.size(), expected.mixerPairs * 2);
                CHECK_EQ_U32(ctx, aux->outputs.size(), 1);
                CHECK_EQ_U32(ctx, aux->crosspoints.size(), expected.mixerPairs);

                // A port exists only when the signal does (AUAA 19): no ghost
                // ADAT pairs left behind by S/MUX or an optical mode change.
                const uint32_t digIn = expected.mixerPairs - 6;
                CHECK(ctx, hasPort(t, 10 + digIn));
                CHECK(ctx, !hasPort(t, 11 + digIn));

                // The connector choice exists only where the device offers one:
                // in S/PDIF format there are two jacks for one digital input,
                // in ADAT format there is only the optical one. Confirmed from
                // the M-Audio Panel -- CFW1814HardwareView::AdaptPortsToSettings
                // disables the "active input" group when the optical setting
                // is 2 (= ADAT).
                const RouterNode* digitalSelector = nullptr;
                for (const auto& node : t.nodes) {
                    if (node.name == "Digital Input Source") {
                        digitalSelector = std::get_if<RouterNode>(&node.body);
                    }
                }
                CHECK(ctx, (digitalSelector != nullptr) == (in == OpticalMode::Spdif));
                if (digitalSelector != nullptr) {
                    CHECK_EQ_U32(ctx, digitalSelector->legalBundles.size(), 2);
                    CHECK_EQ_U32(ctx, *digitalSelector->constraints.maxActiveBundles, 1);
                }

                auto state = Devices::FW1814::makeInitialState(*resolved);
                CHECK(ctx, validateState(t, state).has_value());
            }
        }
    }

    const DeviceConfiguration adat48{
        .sampleRate = 48000,
        .opticalInput = OpticalMode::Adat,
        .opticalOutput = OpticalMode::Adat,
    };
    auto resolved = Devices::FW1814::resolve(adat48);
    REQUIRE(ctx, resolved.has_value());
    const auto& t = resolved->topology;

    // Level sits on the mixer input port, not the crosspoint: the parameter
    // window holds one gain per input channel shared by both mixer outputs,
    // while the crosspoints are the 0x90/0x94 routing bits (AUAA 13.1).
    {
        uint32_t levelOnPort = 0;
        uint32_t muteOnCrosspoint = 0;
        uint32_t levelOnCrosspoint = 0;
        for (const auto& parameter : t.parameters) {
            const bool onCrosspoint = std::holds_alternative<CrosspointId>(parameter.target);
            if (parameter.semantic == ParameterSemantic::Level && !onCrosspoint) ++levelOnPort;
            if (parameter.semantic == ParameterSemantic::Level && onCrosspoint) ++levelOnCrosspoint;
            if (parameter.semantic == ParameterSemantic::Mute && onCrosspoint) ++muteOnCrosspoint;
        }
        CHECK_EQ_U32(ctx, levelOnCrosspoint, 0);
        CHECK_EQ_U32(ctx, muteOnCrosspoint, 20);  // 10 pairs x 2 mixer outputs
        CHECK(ctx, levelOnPort > 0);
    }

    // The device powers up with an empty mixer: 0x90 = 0 and 0x94 = 0x00000009,
    // i.e. only stream pair 0 -> mixer 0 and pair 1 -> mixer 1. That silence is
    // what documentation/1814.md 2.2 root-caused, so the fixture reproduces it.
    {
        auto state = Devices::FW1814::makeInitialState(*resolved);
        const MixerNode* main = mixerNamed(t, "Main Mixer");
        REQUIRE(ctx, main != nullptr);

        uint32_t enabled = 0;
        for (const auto& parameter : t.parameters) {
            if (parameter.semantic != ParameterSemantic::Mute) continue;
            if (!std::holds_alternative<CrosspointId>(parameter.target)) continue;
            if (!std::get<bool>(state.parameters.at(parameter.id))) ++enabled;
        }
        CHECK_EQ_U32(ctx, enabled, 2);
    }

    // Clock source carries the wire values, not invented ones
    // (bebob_maudio.c:342-348); Linux selects 3 = Internal at discovery.
    {
        auto state = Devices::FW1814::makeInitialState(*resolved);
        const Parameter* clock = nullptr;
        for (const auto& parameter : t.parameters) {
            if (parameter.semantic == ParameterSemantic::ClockSource) clock = &parameter;
        }
        REQUIRE(ctx, clock != nullptr);
        const auto& items = std::get<EnumDomain>(clock->domain).values;
        REQUIRE(ctx, items.size() == 4);
        CHECK(ctx, items[1].name == "Digital");
        CHECK(ctx, items[2].name == "Word Clock");
        CHECK(ctx, items[3].name == "Internal");
        CHECK(ctx, std::get<int64_t>(state.parameters.at(clock->id)) == 3);
    }

    // Negative: two fixed drivers on one input port is illegal.
    {
        auto invalid = t;
        invalid.fixedLinks.push_back(FixedLink{PortId{2}, PortId{21}});
        auto result = validate(invalid);
        CHECK(ctx, !result.has_value());
        if (!result.has_value()) {
            CHECK(ctx, result.error().kind == TopologyErrorKind::MultipleDriversOnInput);
        }
    }
}

} // namespace ASFW::LabTests
