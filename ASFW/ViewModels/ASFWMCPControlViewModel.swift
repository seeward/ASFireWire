import Combine
import Foundation

/// Owns the process's one loopback MCP listener. SwiftUI may recreate views
/// and their observable models while the app remains alive; a listener cannot
/// be recreated on the same port. Keeping ownership here lets every model
/// observe the same host instead of treating EADDRINUSE as a startup failure.
@MainActor
private final class ASFWMCPHostCoordinator: ObservableObject {
    static let shared = ASFWMCPHostCoordinator()

    @Published private(set) var status: ASFWMCPHostStatus = .stopped

    private var host: ASFWMCPHost<LiveASFWDriverControl>?
    private var startTask: Task<ASFWMCPHostStatus, Error>?

    func start(connector: ASFWDriverConnector,
               configuration: ASFWMCPHostConfiguration) async throws -> ASFWMCPHostStatus {
        if status.isRunning {
            return status
        }
        if let startTask {
            return try await startTask.value
        }

        let driver = LiveASFWDriverControl(backend: connector)
        let core = ASFWMCPCore(
            configuration: ASFWMCPRuntimeConfiguration(
                mode: .unrestrictedWrite,
                writePolicyAvailable: true,
                swiftTestGatePassed: true,
                rawDeveloperTierEnabled: true
            ),
            driver: driver
        )
        let nextHost = ASFWMCPHost(core: core)
        nextHost.onStatusChanged = { [weak self] status in
            self?.status = status
        }
        host = nextHost

        let task = Task { @MainActor [weak self, nextHost] () throws -> ASFWMCPHostStatus in
            do {
                let nextStatus = try await nextHost.start(configuration: configuration)
                self?.status = nextStatus
                return nextStatus
            } catch {
                self?.host = nil
                self?.status = .stopped
                throw error
            }
        }
        startTask = task
        defer { startTask = nil }
        return try await task.value
    }

    func stop() async {
        if let startTask {
            _ = try? await startTask.value
        }
        await host?.stop()
        host = nil
        status = .stopped
    }
}

@MainActor
final class ASFWMCPControlViewModel: ObservableObject {
    @Published var isEnabled: Bool
    @Published var portText: String
    @Published var guardedFCPExperimentsEnabled: Bool
    @Published private(set) var status: ASFWMCPHostStatus = .stopped
    @Published private(set) var isChangingState = false
    @Published private(set) var lastError: String?
    @Published private(set) var hardwareSmokeReport: ASFWMCPHardwareSmokeReport?

    private let connector: ASFWDriverConnector
    private let defaults: UserDefaults
    private let hostCoordinator: ASFWMCPHostCoordinator
    private var statusSubscription: AnyCancellable?

    private enum DefaultsKey {
        static let enabled = "asfw.mcp.enabled"
        static let port = "asfw.mcp.port"
        static let guardedFCPExperimentsEnabled = "asfw.mcp.guarded-fcp-experiments-enabled"
    }

    init(connector: ASFWDriverConnector, defaults: UserDefaults = .standard) {
        self.connector = connector
        self.defaults = defaults
        self.hostCoordinator = .shared
        let savedPort = defaults.integer(forKey: DefaultsKey.port)
        self.portText = savedPort > 0 ? "\(savedPort)" : "8765"
        self.isEnabled = defaults.bool(forKey: DefaultsKey.enabled)
        self.guardedFCPExperimentsEnabled = defaults.bool(forKey: DefaultsKey.guardedFCPExperimentsEnabled)
        self.status = self.hostCoordinator.status
        self.statusSubscription = self.hostCoordinator.$status.sink { [weak self] status in
            self?.status = status
        }
    }

    var endpointText: String {
        status.endpointURL?.absoluteString ?? "Not running"
    }

    var sessionText: String {
        guard status.isRunning else { return "Stopped" }
        return status.activeHTTPConnections > 0 ? "Session active" : "Waiting for agent"
    }

    var canEditPort: Bool {
        status.isRunning == false && isChangingState == false
    }

    var canRunReadOnlyHardwareSmoke: Bool {
        status.isRunning && isChangingState == false
    }

    var canEditGuardedFCPExperiments: Bool {
        status.isRunning == false && isChangingState == false
    }

    func setGuardedFCPExperimentsEnabled(_ enabled: Bool) {
        guardedFCPExperimentsEnabled = enabled
        defaults.set(enabled, forKey: DefaultsKey.guardedFCPExperimentsEnabled)
    }

    func applyEnabledState() {
        Task { await setEnabled(isEnabled) }
    }

    func setEnabled(_ enabled: Bool) async {
        defaults.set(enabled, forKey: DefaultsKey.enabled)
        isEnabled = enabled
        if enabled {
            await start()
        } else {
            await stop()
        }
    }

    func start() async {
        guard status.isRunning == false else { return }
        guard let port = UInt16(portText.trimmingCharacters(in: .whitespacesAndNewlines)) else {
            lastError = "Port must be a number from 0 to 65535."
            isEnabled = false
            defaults.set(false, forKey: DefaultsKey.enabled)
            return
        }

        isChangingState = true
        lastError = nil
        defaults.set(Int(port), forKey: DefaultsKey.port)

        do {
            status = try await hostCoordinator.start(
                connector: connector,
                configuration: ASFWMCPHostConfiguration(port: port)
            )
        } catch {
            lastError = "Failed to start MCP host: \(error.localizedDescription)"
            status = .stopped
            isEnabled = false
            defaults.set(false, forKey: DefaultsKey.enabled)
        }

        isChangingState = false
    }

    func stop() async {
        isChangingState = true
        lastError = nil
        await hostCoordinator.stop()
        isChangingState = false
    }

    func runReadOnlyHardwareSmoke() async {
        guard canRunReadOnlyHardwareSmoke else { return }

        isChangingState = true
        lastError = nil
        let driver = LiveASFWDriverControl(backend: connector)
        let core = ASFWMCPCore(configuration: .readOnlyDeveloper, driver: driver)
        let report = await ASFWMCPHardwareSmokeRunner(core: core).run(options: .readOnly)
        hardwareSmokeReport = report
        if report.failures.isEmpty == false {
            lastError = "Hardware smoke found \(report.failures.count) failure(s): \(report.conciseSummary)"
        }
        isChangingState = false
    }
}
