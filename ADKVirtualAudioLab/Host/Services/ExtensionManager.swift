import Foundation
import SystemExtensions

final class ExtensionManager: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    static let dextIdentifier: String = {
        let dir = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Library/SystemExtensions")
        if let items = try? FileManager.default.contentsOfDirectory(
            at: dir, includingPropertiesForKeys: nil) {
            for url in items where url.pathExtension == "dext" {
                if let identifier = Bundle(url: url)?.bundleIdentifier {
                    return identifier
                }
            }
        }
        return "net.mrmidi.ASFW.ADKVirtualAudioLab"
    }()

    @Published var status = "Idle."

    func activate() {
        submit(OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: Self.dextIdentifier, queue: .main))
    }

    func deactivate() {
        submit(OSSystemExtensionRequest.deactivationRequest(
            forExtensionWithIdentifier: Self.dextIdentifier, queue: .main))
    }

    private func submit(_ request: OSSystemExtensionRequest) {
        request.delegate = self
        status = "Request submitted…"
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func request(_ request: OSSystemExtensionRequest,
                 actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension ext: OSSystemExtensionProperties)
        -> OSSystemExtensionRequest.ReplacementAction {
        status = "Replacing \(existing.bundleShortVersion) with \(ext.bundleShortVersion)…"
        return .replace
    }

    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        status = "Needs approval in System Settings → General → Login Items & Extensions."
    }

    func request(_ request: OSSystemExtensionRequest,
                 didFinishWithResult result: OSSystemExtensionRequest.Result) {
        switch result {
        case .completed:
            status = "Completed. Check Audio MIDI Setup for the virtual device."
        case .willCompleteAfterReboot:
            status = "Will complete after reboot."
        @unknown default:
            status = "Finished with unknown result (\(result.rawValue))."
        }
    }

    func request(_ request: OSSystemExtensionRequest,
                 didFailWithError error: Error) {
        status = "Failed: \(error.localizedDescription)"
    }
}
