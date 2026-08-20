#include "TestHarness.hpp"

#include <cstdio>

namespace ASFW::LabTests {

void RunPcmSlotCodecTests(TestContext& ctx);
void RunCipHeaderTests(TestContext& ctx);
void RunDbcCounterTests(TestContext& ctx);
void RunSytTests(TestContext& ctx);
void RunCadenceTests(TestContext& ctx);
void RunPacketTimelineTests(TestContext& ctx);
void RunPacketizerTests(TestContext& ctx);
void RunPayloadWriterTests(TestContext& ctx);
void RunDiceTxEngineTests(TestContext& ctx);
void RunVerifyingSlotProviderTests(TestContext& ctx);
void RunVerifierScenarioTests(TestContext& ctx);
void RunTxTimingModelTests(TestContext& ctx);
void RunWriteEndTraceReplayerTests(TestContext& ctx);
void RunPacketDumpBlobTests(TestContext& ctx);
void RunNamingTests(TestContext& ctx);
void RunDuetTopologyTests(TestContext& ctx);
void RunPhase88TopologyTests(TestContext& ctx);
void RunFW1814TopologyTests(TestContext& ctx);
void RunSaffireTopologyTests(TestContext& ctx);
void RunVirtualDeviceRuntimeTests(TestContext& ctx);
void RunLabEventLogTests(TestContext& ctx);
void RunProjectionTests(TestContext& ctx);
void RunDeviceConfigurationStateMachineTests(TestContext& ctx);
void RunLabConfigurationCoordinatorTests(TestContext& ctx);

} // namespace ASFW::LabTests

int main() {
    using namespace ASFW::LabTests;

    TestContext ctx{};

    RunPcmSlotCodecTests(ctx);
    RunCipHeaderTests(ctx);
    RunDbcCounterTests(ctx);
    RunSytTests(ctx);
    RunCadenceTests(ctx);
    RunPacketTimelineTests(ctx);
    RunPacketizerTests(ctx);
    RunPayloadWriterTests(ctx);
    RunDiceTxEngineTests(ctx);
    RunVerifyingSlotProviderTests(ctx);
    RunTxTimingModelTests(ctx);
    RunWriteEndTraceReplayerTests(ctx);
    RunVerifierScenarioTests(ctx);
    RunPacketDumpBlobTests(ctx);
    RunNamingTests(ctx);
    RunDuetTopologyTests(ctx);
    RunPhase88TopologyTests(ctx);
    RunFW1814TopologyTests(ctx);
    RunSaffireTopologyTests(ctx);
    RunVirtualDeviceRuntimeTests(ctx);
    RunLabEventLogTests(ctx);
    RunProjectionTests(ctx);
    RunDeviceConfigurationStateMachineTests(ctx);
    RunLabConfigurationCoordinatorTests(ctx);

    std::printf("%d checks, %d failures\n", ctx.checks, ctx.failures);
    return ctx.failures == 0 ? 0 : 1;
}
