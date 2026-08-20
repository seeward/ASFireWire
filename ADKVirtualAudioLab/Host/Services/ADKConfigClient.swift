import Foundation
import IOKit
import OSLog

enum ADKConfigTrace {
    private static let logger = Logger(
        subsystem: "net.mrmidi.ASFW.ADKLab",
        category: "ADKConfig")

    static func emit(_ message: String) {
        logger.info("[ADKConfigHost] \(message, privacy: .public)")
        FileHandle.standardError.write(
            Data("[ADKConfigHost] \(message)\n".utf8))
    }
}

// Host mirror of Lab/ADKConfigChange.hpp. This is intentionally a native,
// same-machine wire format: the diagnostic user client is a research
// instrument, not a public ABI.
enum ADKConfigWire {
    static let magic: UInt32 = 0x4C43_4647 // 'LCFG'
    static let version: UInt32 = 2
    static let deviceCount = 4
    static let userClientType: UInt32 = 0x4C44_4247 // 'LDBG'

    static let selectorRequestSampleRate: UInt32 = 1
    static let selectorCopyConfigLog: UInt32 = 2
    static let selectorCopyConfigState: UInt32 = 3
    static let selectorRequestConfiguration: UInt32 = 4
    static let selectorSetHardwareOutcome: UInt32 = 5
    static let selectorNotifyHardwareObserved: UInt32 = 6

    static let logHeaderSize = 64
    static let eventSize = 56
    static let stateSize = 72
    // Keep this below 4 KiB. The live control plane rejected the previous
    // 96-event (5440-byte) request; 64 events produce 3648 bytes.
    static let maxEvents: UInt32 = 64
}

struct ADKConfigEvent: Identifiable, Sendable {
    let sequence: UInt64
    let hostTimeTicks: UInt64
    let deviceSlot: UInt32
    let deviceObjectID: UInt32
    let action: UInt64
    let phase: UInt32
    let result: Int32
    let oldSampleRate: UInt32
    let newSampleRate: UInt32
    let configurationPending: Bool
    let oldChannelCount: UInt32
    let newChannelCount: UInt32

    var id: UInt64 { sequence }

    var phaseName: String {
        switch phase {
        case 1: return "HostRequest"
        case 2: return "RequestCalled"
        case 3: return "RequestReturned"
        case 4: return "RequestRejected"
        case 5: return "StopIOEnter"
        case 6: return "StopIOReturn"
        case 7: return "PerformEnter"
        case 8: return "PerformMutation"
        case 9: return "PerformSuper"
        case 10: return "PerformReturn"
        case 11: return "AbortEnter"
        case 12: return "AbortSuper"
        case 13: return "AbortReturn"
        case 14: return "StartIOEnter"
        case 15: return "StartIOReturn"
        case 16: return "HandleSampleRateEnter"
        case 17: return "HandleSampleRateReturn"
        case 18: return "DeviceRateMutation"
        case 19: return "OutputStreamMutation"
        case 20: return "InputStreamMutation"
        case 21: return "CandidateAccepted"
        case 22: return "CandidateRejected"
        case 23: return "HardwareApply"
        case 24: return "HardwareCompleted"
        case 25: return "HardwareUnchanged"
        case 26: return "HardwareUnknown"
        case 27: return "ProjectionCommitted"
        case 28: return "CoordinatorRejected"
        case 29: return "HardwareObserved"
        default: return "Phase \(phase)"
        }
    }

    var resultName: String {
        result == 0 ? "success" : String(format: "0x%08x", UInt32(bitPattern: result))
    }

    var mutationSummary: String {
        let rates = "\(oldSampleRate)→\(newSampleRate) Hz"
        guard oldChannelCount != 0 || newChannelCount != 0 else { return rates }
        return rates + " · \(oldChannelCount)→\(newChannelCount) ch"
    }

    var displayText: String {
        String(sequence) + "  " + phaseName + "  " + mutationSummary +
            "  " + resultName
    }
}

struct ADKConfigState: Sendable {
    let deviceSlot: UInt32
    let deviceObjectID: UInt32
    let currentSampleRate: UInt32
    let pendingSampleRate: UInt32
    let configurationPending: Bool
    let currentOpticalInput: UInt32
    let currentOpticalOutput: UInt32
    let pendingOpticalInput: UInt32
    let pendingOpticalOutput: UInt32
    let currentInputChannels: UInt32
    let currentOutputChannels: UInt32
    let pendingAction: UInt64
    let nextSequence: UInt64
}

struct ADKConfigDeviceRow: Identifiable, Sendable {
    let slot: Int
    let name: String
    var state: ADKConfigState?
    var events: [ADKConfigEvent]

    var id: Int { slot }
    var currentRateText: String {
        guard let state else { return "unknown" }
        return "\(state.currentSampleRate) Hz"
    }
    var pendingText: String {
        guard let state, state.configurationPending else { return "idle" }
        return "pending \(state.pendingSampleRate) Hz · \(state.pendingOpticalInput)/\(state.pendingOpticalOutput)"
    }
    var eventSummary: String {
        events.suffix(8).map(\.displayText).joined(separator: "\n")
    }
}

enum ADKConfigClientError: LocalizedError {
    case serviceNotFound
    case openFailed(kern_return_t)
    case callFailed(kern_return_t)
    case malformed(String)

    var errorDescription: String? {
        switch self {
        case .serviceNotFound:
            return "AudioDriverKit lab service not found — activate the dext first."
        case .openFailed(let kr):
            return String(format: "Configuration userclient open failed (0x%08x).", kr)
        case .callFailed(let kr):
            return String(format: "Configuration userclient call failed (0x%08x).", kr)
        case .malformed(let reason):
            return "Malformed ADK configuration response: \(reason)"
        }
    }
}

final class ADKConfigClient {
    private var connection: io_connect_t = IO_OBJECT_NULL

    deinit { close() }

    private func close() {
        if connection != IO_OBJECT_NULL {
            IOServiceClose(connection)
            connection = IO_OBJECT_NULL
        }
    }

    private func findService() -> io_service_t {
        let byName = IOServiceGetMatchingService(
            kIOMainPortDefault, IOServiceNameMatching("VirtualAudioDriver"))
        if byName != IO_OBJECT_NULL { return byName }

        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(
            kIOMainPortDefault, IOServiceMatching("IOUserService"),
            &iterator) == KERN_SUCCESS else { return IO_OBJECT_NULL }
        defer { IOObjectRelease(iterator) }

        while case let service = IOIteratorNext(iterator), service != IO_OBJECT_NULL {
            if let name = IORegistryEntryCreateCFProperty(
                service, "IOUserServerName" as CFString, kCFAllocatorDefault, 0)?
                .takeRetainedValue() as? String,
                name.contains("ADKVirtualAudioLab") {
                return service
            }
            IOObjectRelease(service)
        }
        return IO_OBJECT_NULL
    }

    private func ensureConnection() throws {
        guard connection == IO_OBJECT_NULL else { return }
        let service = findService()
        guard service != IO_OBJECT_NULL else {
            throw ADKConfigClientError.serviceNotFound
        }
        defer { IOObjectRelease(service) }
        let kr = IOServiceOpen(service, mach_task_self_,
                               ADKConfigWire.userClientType, &connection)
        guard kr == KERN_SUCCESS, connection != IO_OBJECT_NULL else {
            connection = IO_OBJECT_NULL
            throw ADKConfigClientError.openFailed(kr)
        }
    }

    func requestSampleRate(slot: Int, rate: UInt32) throws {
        try ensureConnection()
        ADKConfigTrace.emit("request slot=\(slot) rate=\(rate)")
        var scalars: [UInt64] = [UInt64(slot), UInt64(rate)]
        var output: [UInt64] = [0]
        var outputCount: UInt32 = 1

        let kr = scalars.withUnsafeMutableBufferPointer { inputs in
            output.withUnsafeMutableBufferPointer { outputs in
                IOConnectCallMethod(
                    connection,
                    ADKConfigWire.selectorRequestSampleRate,
                    inputs.baseAddress,
                    UInt32(inputs.count),
                    nil,
                    0,
                    outputs.baseAddress,
                    &outputCount,
                    nil,
                    nil)
            }
        }
        guard kr == KERN_SUCCESS else {
            ADKConfigTrace.emit(String(
                format: "request slot=%d rate=%u failed=0x%08x",
                slot, rate, kr))
            if kr == kIOReturnNotOpen || kr == kIOReturnNoDevice { close() }
            throw ADKConfigClientError.callFailed(kr)
        }
        ADKConfigTrace.emit("request slot=\(slot) rate=\(rate) accepted")
    }

    func requestConfiguration(slot: Int, rate: UInt32,
                              opticalInput: UInt32,
                              opticalOutput: UInt32) throws {
        try ensureConnection()
        ADKConfigTrace.emit(
            "request slot=\(slot) rate=\(rate) optical=\(opticalInput)/\(opticalOutput)")
        var scalars: [UInt64] = [
            UInt64(slot), UInt64(rate), UInt64(opticalInput), UInt64(opticalOutput),
        ]
        var output: [UInt64] = [0]
        var outputCount: UInt32 = 1
        let kr = scalars.withUnsafeMutableBufferPointer { inputs in
            output.withUnsafeMutableBufferPointer { outputs in
                IOConnectCallMethod(
                    connection,
                    ADKConfigWire.selectorRequestConfiguration,
                    inputs.baseAddress,
                    UInt32(inputs.count),
                    nil,
                    0,
                    outputs.baseAddress,
                    &outputCount,
                    nil,
                    nil)
            }
        }
        guard kr == KERN_SUCCESS else {
            ADKConfigTrace.emit(String(
                format: "request slot=%d rate=%u optical=%u/%u failed=0x%08x",
                slot, rate, opticalInput, opticalOutput, kr))
            if kr == kIOReturnNotOpen || kr == kIOReturnNoDevice { close() }
            throw ADKConfigClientError.callFailed(kr)
        }
        ADKConfigTrace.emit(
            "request slot=\(slot) rate=\(rate) optical=\(opticalInput)/\(opticalOutput) accepted")
    }

    func setHardwareOutcome(slot: Int, outcome: UInt32) throws {
        try ensureConnection()
        var scalars: [UInt64] = [UInt64(slot), UInt64(outcome)]
        let kr = scalars.withUnsafeMutableBufferPointer { inputs in
            IOConnectCallMethod(
                connection,
                ADKConfigWire.selectorSetHardwareOutcome,
                inputs.baseAddress,
                UInt32(inputs.count),
                nil,
                0,
                nil,
                nil,
                nil,
                nil)
        }
        guard kr == KERN_SUCCESS else {
            if kr == kIOReturnNotOpen || kr == kIOReturnNoDevice { close() }
            throw ADKConfigClientError.callFailed(kr)
        }
        ADKConfigTrace.emit("script hardware slot=\(slot) outcome=\(outcome)")
    }

    func notifyHardwareObserved(slot: Int, rate: UInt32,
                                opticalInput: UInt32,
                                opticalOutput: UInt32) throws {
        try ensureConnection()
        var scalars: [UInt64] = [
            UInt64(slot), UInt64(rate), UInt64(opticalInput), UInt64(opticalOutput),
        ]
        var output: [UInt64] = [0]
        var outputCount: UInt32 = 1
        let kr = scalars.withUnsafeMutableBufferPointer { inputs in
            output.withUnsafeMutableBufferPointer { outputs in
                IOConnectCallMethod(
                    connection,
                    ADKConfigWire.selectorNotifyHardwareObserved,
                    inputs.baseAddress,
                    UInt32(inputs.count),
                    nil,
                    0,
                    outputs.baseAddress,
                    &outputCount,
                    nil,
                    nil)
            }
        }
        guard kr == KERN_SUCCESS else {
            if kr == kIOReturnNotOpen || kr == kIOReturnNoDevice { close() }
            throw ADKConfigClientError.callFailed(kr)
        }
        ADKConfigTrace.emit(
            "observe hardware slot=\(slot) rate=\(rate) optical=\(opticalInput)/\(opticalOutput)")
    }

    func state(slot: Int) throws -> ADKConfigState {
        try ensureConnection()
        let data = try callStructure(
            selector: ADKConfigWire.selectorCopyConfigState,
            scalars: [UInt64(slot)],
            capacity: ADKConfigWire.stateSize)
        return try Self.parseState(data)
    }

    func events(slot: Int, maxEvents: UInt32 = 32) throws -> [ADKConfigEvent] {
        try ensureConnection()
        let clamped = min(max(maxEvents, 1), ADKConfigWire.maxEvents)
        let data = try callStructure(
            selector: ADKConfigWire.selectorCopyConfigLog,
            scalars: [UInt64(slot), UInt64(clamped)],
            capacity: ADKConfigWire.logHeaderSize
                + Int(clamped) * ADKConfigWire.eventSize)
        return try Self.parseEvents(data)
    }

    private func callStructure(selector: UInt32,
                               scalars: [UInt64],
                               capacity: Int) throws -> Data {
        var blob = Data(count: capacity)
        var outputSize = capacity
        let kr = scalars.withUnsafeBufferPointer { inputs in
            blob.withUnsafeMutableBytes { raw in
                IOConnectCallMethod(
                    connection,
                    selector,
                    inputs.baseAddress,
                    UInt32(inputs.count),
                    nil,
                    0,
                    nil,
                    nil,
                    raw.baseAddress,
                    &outputSize)
            }
        }
        guard kr == KERN_SUCCESS else {
            if kr == kIOReturnNotOpen || kr == kIOReturnNoDevice { close() }
            throw ADKConfigClientError.callFailed(kr)
        }
        guard outputSize <= blob.count else {
            throw ADKConfigClientError.malformed("structure output is too large")
        }
        blob.removeSubrange(outputSize..<blob.count)
        return blob
    }

    private static func parseState(_ data: Data) throws -> ADKConfigState {
        guard data.count >= ADKConfigWire.stateSize else {
            throw ADKConfigClientError.malformed("state is short")
        }
        return try data.withUnsafeBytes { raw in
            guard u32(raw, 0) == ADKConfigWire.magic,
                  u32(raw, 4) == ADKConfigWire.version else {
                throw ADKConfigClientError.malformed("bad state header")
            }
            return ADKConfigState(
                deviceSlot: u32(raw, 12),
                deviceObjectID: u32(raw, 16),
                currentSampleRate: u32(raw, 20),
                pendingSampleRate: u32(raw, 24),
                configurationPending: u32(raw, 28) != 0,
                currentOpticalInput: u32(raw, 32),
                currentOpticalOutput: u32(raw, 36),
                pendingOpticalInput: u32(raw, 40),
                pendingOpticalOutput: u32(raw, 44),
                currentInputChannels: u32(raw, 48),
                currentOutputChannels: u32(raw, 52),
                pendingAction: u64(raw, 56),
                nextSequence: u64(raw, 64))
        }
    }

    private static func parseEvents(_ data: Data) throws -> [ADKConfigEvent] {
        guard data.count >= ADKConfigWire.logHeaderSize else {
            throw ADKConfigClientError.malformed("log header is short")
        }
        return try data.withUnsafeBytes { raw in
            guard u32(raw, 0) == ADKConfigWire.magic,
                  u32(raw, 4) == ADKConfigWire.version else {
                throw ADKConfigClientError.malformed("bad log header")
            }
            let count = Int(u32(raw, 8))
            let stride = Int(u32(raw, 12))
            guard stride == ADKConfigWire.eventSize,
                  count >= 0,
                  data.count >= ADKConfigWire.logHeaderSize + count * stride else {
                throw ADKConfigClientError.malformed("invalid log count/stride")
            }

            return (0..<count).map { index in
                let base = ADKConfigWire.logHeaderSize + index * stride
                let channelCounts = u32(raw, base + 52)
                return ADKConfigEvent(
                    sequence: u64(raw, base),
                    hostTimeTicks: u64(raw, base + 8),
                    deviceSlot: u32(raw, base + 16),
                    deviceObjectID: u32(raw, base + 20),
                    action: u64(raw, base + 24),
                    phase: u32(raw, base + 32),
                    result: Int32(bitPattern: u32(raw, base + 36)),
                    oldSampleRate: u32(raw, base + 40),
                    newSampleRate: u32(raw, base + 44),
                    configurationPending: u32(raw, base + 48) != 0,
                    oldChannelCount: channelCounts & 0xFFFF,
                    newChannelCount: channelCounts >> 16)
            }
        }
    }
}

private func u32(_ raw: UnsafeRawBufferPointer, _ offset: Int) -> UInt32 {
    raw.loadUnaligned(fromByteOffset: offset, as: UInt32.self)
}

private func u64(_ raw: UnsafeRawBufferPointer, _ offset: Int) -> UInt64 {
    raw.loadUnaligned(fromByteOffset: offset, as: UInt64.self)
}

// IOKit doesn't surface these in Swift.
private let kIOReturnNotOpen: kern_return_t = -536870195
private let kIOReturnNoDevice: kern_return_t = -536870208
