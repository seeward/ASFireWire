import Combine
import Darwin.Mach
import Foundation

@MainActor
final class MAudio1814ConfigurationViewModel: ObservableObject {
    @Published private(set) var snapshot: AudioConfigurationSnapshot?
    @Published var selectedRateHz: UInt32 = 48_000
    @Published var selectedInputOptical: AudioOpticalMode = .spdif
    @Published var selectedOutputOptical: AudioOpticalMode = .spdif
    @Published private(set) var statusText = "Looking for a FireWire 1814…"
    @Published private(set) var isApplying = false

    private let connector: ASFWDriverConnector
    private var requestDeadline: Date?

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    func poll() async {
        while !Task.isCancelled {
            refresh()
            try? await Task.sleep(for: .seconds(1))
        }
    }

    func apply() {
        guard let snapshot else { return }
        guard !(selectedRateHz == snapshot.committed.sampleRateHz &&
                selectedInputOptical == snapshot.committed.inputOptical &&
                selectedOutputOptical == snapshot.committed.outputOptical) else {
            statusText = "That configuration is already committed."
            return
        }
        isApplying = true
        requestDeadline = .now.addingTimeInterval(8)
        statusText = "Requesting configuration…"
        let result = connector.requestAudioConfiguration(
            endpointID: snapshot.endpointID,
            sampleRateHz: selectedRateHz,
            inputOptical: selectedInputOptical,
            outputOptical: selectedOutputOptical
        )
        if result == KERN_SUCCESS {
            statusText = "Requested; waiting for Core Audio to commit the new geometry."
        } else {
            statusText = "Request rejected: \(connector.interpretIOReturn(result))"
            isApplying = false
            requestDeadline = nil
        }
    }

    var supportedRates: [UInt32] {
        Array(Set(snapshot?.capabilities.map(\.sampleRateHz) ?? [])).sorted()
    }

    private func refresh() {
        let matching = connector.getAudioConfigurationEndpointIDs().lazy.compactMap {
            self.connector.getAudioConfiguration(endpointID: $0)
        }.first
        guard let matching else {
            snapshot = nil
            statusText = "Connect a FireWire 1814 to configure it."
            return
        }
        let changed = matching != snapshot
        snapshot = matching
        if changed {
            selectedRateHz = matching.committed.sampleRateHz
            selectedInputOptical = matching.committed.inputOptical
            selectedOutputOptical = matching.committed.outputOptical
            statusText = "Committed: \(matching.committed.sampleRateHz.formatted()) Hz · \(matching.committed.inputChannels) in / \(matching.committed.outputChannels) out"
            isApplying = false
            requestDeadline = nil
        } else if let requestDeadline, requestDeadline < .now {
            statusText = "No committed change was observed. Check the driver log."
            isApplying = false
            self.requestDeadline = nil
        }
    }
}
