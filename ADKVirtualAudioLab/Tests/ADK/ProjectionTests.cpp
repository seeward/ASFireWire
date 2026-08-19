#include "../TestHarness.hpp"
#include "../../ADK/DeviceProjection.hpp"
#include "../../ADK/StreamProjection.hpp"
#include "../../ADK/ControlProjection.hpp"
#include "../../Runtime/VirtualDeviceRegistry.hpp"
#include "../../Runtime/VirtualDeviceRuntime.hpp"

namespace ASFW::LabTests {

using namespace ASFW::ADK;
using namespace ASFW::Device;
using namespace ASFW::Runtime;

void RunProjectionTests(TestContext& ctx) {
    for (const auto& def : virtualDevices()) {
        auto rtRes = VirtualDeviceRuntime::create(def.kind);
        REQUIRE(ctx, rtRes.has_value());
        const auto& rt = *rtRes;

        // 1. Device Projection
        auto devProps = projectDevice(rt.capabilities(), rt.resolved());
        CHECK(ctx, !devProps.name.empty());
        CHECK(ctx, !devProps.deviceUID.empty());
        CHECK(ctx, devProps.currentSampleRate == 48000.0);
        CHECK(ctx, !devProps.availableSampleRates.empty());
        CHECK(ctx, devProps.transportType == 0x31333934); // '1394'

        // 2. Stream Projection
        auto streamProj = projectStreams(rt.resolved().streams);
        CHECK(ctx, streamProj.sampleRate == 48000.0);
        CHECK(ctx, !streamProj.streams.empty());
        CHECK(ctx, streamProj.totalCaptureChannels > 0);
        CHECK(ctx, streamProj.totalPlaybackChannels > 0);

        for (const auto& s : streamProj.streams) {
            CHECK(ctx, s.channelCount > 0);
            CHECK(ctx, s.format.formatID == 0x6c70636d); // 'lpcm'
            CHECK(ctx, s.format.bitsPerChannel == 32);
            CHECK(ctx, s.format.channelsPerFrame == s.channelCount);
            CHECK(ctx, s.format.bytesPerFrame == s.channelCount * sizeof(float));
        }

        // 3. Control Projection
        auto controls = projectControls(rt.resolved().topology, rt.state());
        CHECK(ctx, controls.size() == rt.resolved().topology.parameters.size());

        for (const auto& c : controls) {
            CHECK(ctx, !c.name.empty());
            switch (c.kind) {
                case ProjectedControlKind::Volume:
                    CHECK(ctx, std::holds_alternative<ProjectedVolumeControl>(c.data));
                    break;
                case ProjectedControlKind::Mute:
                    CHECK(ctx, std::holds_alternative<ProjectedMuteControl>(c.data));
                    break;
                case ProjectedControlKind::Boolean:
                    CHECK(ctx, std::holds_alternative<ProjectedBooleanControl>(c.data));
                    break;
                case ProjectedControlKind::Enum:
                    CHECK(ctx, std::holds_alternative<ProjectedEnumControl>(c.data));
                    break;
            }
        }
    }
}

} // namespace ASFW::LabTests
