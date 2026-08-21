import Testing
@testable import ASFW

struct DriverStatusRefreshPolicyTests {
    @Test
    func lifecycleAndTopologyChangesInvalidateSnapshots() {
        #expect(DriverConnectorSharedStatusReason.boot.invalidatesControllerSnapshot)
        #expect(DriverConnectorSharedStatusReason.busReset.invalidatesControllerSnapshot)
        #expect(DriverConnectorSharedStatusReason.manual.invalidatesControllerSnapshot)
        #expect(DriverConnectorSharedStatusReason.disconnect.invalidatesControllerSnapshot)
    }

    @Test
    func telemetryPulsesNeverTriggerRecursiveReads() {
        #expect(!DriverConnectorSharedStatusReason.interrupt.invalidatesControllerSnapshot)
        #expect(!DriverConnectorSharedStatusReason.asyncActivity.invalidatesControllerSnapshot)
        #expect(!DriverConnectorSharedStatusReason.watchdog.invalidatesControllerSnapshot)
        #expect(!DriverConnectorSharedStatusReason.unknown.invalidatesControllerSnapshot)
    }
}
