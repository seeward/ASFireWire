import Foundation
import IOKit

extension ASFWDriverConnector {
    func requestAudioConfigurationSnapshotAsync(
        completion: @escaping (AudioConfigurationSnapshot?, kern_return_t) -> Void
    ) {
        connectionQueue.async { [weak self] in
            guard let self, self.connection != 0,
                  self.asyncPort != mach_port_t(MACH_PORT_NULL) else {
                DispatchQueue.main.async { completion(nil, kIOReturnNotReady) }
                return
            }
            let requestID = self.nextAudioControlRequestID
            self.nextAudioControlRequestID &+= 1
            self.audioConfigurationSnapshotCompletions[requestID] = { status, values in
                let snapshot = status == KERN_SUCCESS
                    ? AudioAsyncSnapshotDecoder.configuration(values) : nil
                completion(snapshot, status)
            }
            var reference = DriverKitAsyncCompletionDecoder.reference(
                marker: Self.audioConfigurationSnapshotAsyncReference
            )
            var inputs = [requestID]
            let result = IOConnectCallAsyncScalarMethod(
                self.connection, Method.getAudioConfigurationAsync.rawValue,
                self.asyncPort, &reference, UInt32(reference.count),
                &inputs, UInt32(inputs.count), nil, nil)
            if result != KERN_SUCCESS {
                let callback = self.audioConfigurationSnapshotCompletions.removeValue(forKey: requestID)
                DispatchQueue.main.async { callback?(result, []) }
            }
        }
    }

    func requestAudioControlSurfaceSnapshotAsync(
        endpointID: AudioEndpointID,
        completion: @escaping (AudioControlSurfaceSnapshot?) -> Void
    ) {
        connectionQueue.async { [weak self] in
            guard let self, self.connection != 0,
                  self.asyncPort != mach_port_t(MACH_PORT_NULL), endpointID.rawValue != 0 else {
                DispatchQueue.main.async { completion(nil) }
                return
            }
            let requestID = self.nextAudioControlRequestID
            self.nextAudioControlRequestID &+= 1
            self.audioControlSnapshotCompletions[requestID] = { [weak self] status, values in
                // The async reply carries only a header: an async completion has
                // 16 scalar slots and the 1814's surface is 78 values. Treat it
                // as "the surface is ready at revision N" and read the whole
                // thing over the struct selector, still off the main queue.
                guard status == KERN_SUCCESS,
                      AudioAsyncSnapshotDecoder.controlsHeader(values) != nil,
                      let self else {
                    completion(nil)
                    return
                }
                self.connectionQueue.async {
                    let snapshot = self.getAudioControlSurface(endpointID: endpointID)
                    DispatchQueue.main.async { completion(snapshot) }
                }
            }
            var reference = DriverKitAsyncCompletionDecoder.reference(
                marker: Self.audioControlSnapshotAsyncReference
            )
            var inputs = [endpointID.rawValue, requestID]
            let result = IOConnectCallAsyncScalarMethod(
                self.connection, Method.getAudioControlSurfaceAsync.rawValue,
                self.asyncPort, &reference, UInt32(reference.count),
                &inputs, UInt32(inputs.count), nil, nil)
            if result != KERN_SUCCESS {
                let callback = self.audioControlSnapshotCompletions.removeValue(forKey: requestID)
                DispatchQueue.main.async { callback?(result, []) }
            }
        }
    }

    func requestAudioMeterSnapshotAsync(
        endpointID: AudioEndpointID,
        completion: @escaping (AudioMeterSnapshot?) -> Void
    ) {
        connectionQueue.async { [weak self] in
            guard let self, self.connection != 0,
                  self.asyncPort != mach_port_t(MACH_PORT_NULL), endpointID.rawValue != 0 else {
                DispatchQueue.main.async { completion(nil) }
                return
            }
            let requestID = self.nextAudioControlRequestID
            self.nextAudioControlRequestID &+= 1
            self.audioMeterSnapshotCompletions[requestID] = { status, values in
                completion(status == KERN_SUCCESS ? AudioAsyncSnapshotDecoder.meters(values) : nil)
            }
            var reference = DriverKitAsyncCompletionDecoder.reference(
                marker: Self.audioMeterSnapshotAsyncReference
            )
            var inputs = [endpointID.rawValue, requestID]
            let result = IOConnectCallAsyncScalarMethod(
                self.connection, Method.getAudioMeterSnapshotAsync.rawValue,
                self.asyncPort, &reference, UInt32(reference.count),
                &inputs, UInt32(inputs.count), nil, nil)
            if result != KERN_SUCCESS {
                let callback = self.audioMeterSnapshotCompletions.removeValue(forKey: requestID)
                DispatchQueue.main.async { callback?(result, []) }
            }
        }
    }

    func getAudioConfigurationEndpointIDs() -> [AudioEndpointID] {
        guard isConnected,
              let data = callStruct(.getAudioConfigurationEndpoints, initialCap: 72) else {
            return []
        }
        return AudioConfigurationWireDecoder.decodeEndpointIDs(data)
    }

    func getAudioConfiguration(endpointID: AudioEndpointID) -> AudioConfigurationSnapshot? {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else { return nil }
        var scalarInput = endpointID.rawValue
        var output = Data(count: 160)
        var outputLength = output.count
        let result = output.withUnsafeMutableBytes { outputBytes in
            IOConnectCallMethod(
                connection,
                Method.getAudioConfiguration.rawValue,
                &scalarInput,
                1,
                nil,
                0,
                nil,
                nil,
                outputBytes.baseAddress,
                &outputLength
            )
        }
        guard result == KERN_SUCCESS, outputLength == output.count else { return nil }
        return AudioConfigurationWireDecoder.decode(output)
    }

    func requestAudioConfiguration(
        endpointID: AudioEndpointID,
        sampleRateHz: UInt32,
        inputOptical: AudioOpticalMode,
        outputOptical: AudioOpticalMode
    ) -> kern_return_t {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else {
            return kIOReturnNotReady
        }
        var scalarInputs = [
            endpointID.rawValue,
            UInt64(sampleRateHz),
            UInt64(inputOptical.rawValue),
            UInt64(outputOptical.rawValue),
        ]
        return IOConnectCallScalarMethod(
            connection,
            Method.requestAudioConfiguration.rawValue,
            &scalarInputs,
            UInt32(scalarInputs.count),
            nil,
            nil
        )
    }

    /// Opens the AudioDriverKit configuration window through an async action.
    /// The completion means only that the request was accepted; the actual
    /// Core Audio transaction is observed through later snapshots.
    func requestAudioConfigurationAsync(
        endpointID: AudioEndpointID,
        sampleRateHz: UInt32,
        inputOptical: AudioOpticalMode,
        outputOptical: AudioOpticalMode,
        completion: @escaping (kern_return_t) -> Void
    ) {
        connectionQueue.async { [weak self] in
            guard let self, self.connection != 0,
                  self.asyncPort != mach_port_t(MACH_PORT_NULL), endpointID.rawValue != 0 else {
                DispatchQueue.main.async { completion(kIOReturnNotReady) }
                return
            }
            let requestID = self.nextAudioControlRequestID
            self.nextAudioControlRequestID &+= 1
            self.audioConfigurationCompletions[requestID] = completion
            var reference = DriverKitAsyncCompletionDecoder.reference(
                marker: Self.audioConfigurationAsyncReference
            )
            var inputs = [endpointID.rawValue, UInt64(sampleRateHz),
                          UInt64(inputOptical.rawValue), UInt64(outputOptical.rawValue), requestID]
            let result = IOConnectCallAsyncScalarMethod(
                self.connection, Method.requestAudioConfigurationAsync.rawValue,
                self.asyncPort, &reference, UInt32(reference.count),
                &inputs, UInt32(inputs.count), nil, nil)
            if result != KERN_SUCCESS {
                let callback = self.audioConfigurationCompletions.removeValue(forKey: requestID)
                DispatchQueue.main.async { callback?(result) }
            }
        }
    }

    func getAudioControlSurface(endpointID: AudioEndpointID) -> AudioControlSurfaceSnapshot? {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else { return nil }
        var scalarInput = endpointID.rawValue
        var output = Data(count: 664)
        var outputLength = output.count
        let result = output.withUnsafeMutableBytes { outputBytes in
            IOConnectCallMethod(
                connection,
                Method.getAudioControlSurface.rawValue,
                &scalarInput,
                1,
                nil,
                0,
                nil,
                nil,
                outputBytes.baseAddress,
                &outputLength
            )
        }
        guard result == KERN_SUCCESS, outputLength == output.count else { return nil }
        return AudioControlSurfaceWireDecoder.decode(output)
    }

    func requestAudioControlValue(endpointID: AudioEndpointID,
                                  controlID: MAudio1814ControlID,
                                  value: Int32) -> kern_return_t {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else {
            return kIOReturnNotReady
        }
        var scalarInputs = [endpointID.rawValue, UInt64(controlID.rawValue),
                            UInt64(UInt32(bitPattern: value))]
        return IOConnectCallScalarMethod(
            connection,
            Method.requestAudioControlValue.rawValue,
            &scalarInputs,
            UInt32(scalarInputs.count),
            nil,
            nil
        )
    }
}

private enum AudioConfigurationWireDecoder {
    private static let wireSize = 160
    private static let capabilitySize = 16
    private static let endpointListSize = 72
    private static let maximumEndpointCount = 8

    static func decodeEndpointIDs(_ data: Data) -> [AudioEndpointID] {
        guard data.count == endpointListSize,
              let version = data.u32(at: 0), version == 1,
              let count = data.u32(at: 4), count <= maximumEndpointCount else {
            return []
        }
        return (0..<Int(count)).compactMap { index in
            guard let raw = data.u64(at: 8 + index * 8), raw != 0 else { return nil }
            return AudioEndpointID(rawValue: raw)
        }
    }

    static func decode(_ data: Data) -> AudioConfigurationSnapshot? {
        guard data.count == wireSize,
              let endpointRaw = data.u64(at: 8),
              let count = data.u32(at: 4),
              count <= 8,
              let committed = decodeCapability(data, at: 16) else {
            return nil
        }
        let capabilities = (0..<Int(count)).compactMap {
            decodeCapability(data, at: 32 + ($0 * capabilitySize))
        }
        guard capabilities.count == Int(count) else { return nil }
        return AudioConfigurationSnapshot(
            endpointID: AudioEndpointID(rawValue: endpointRaw),
            committed: committed,
            capabilities: capabilities
        )
    }

    private static func decodeCapability(_ data: Data, at offset: Int) -> AudioConfigurationCapability? {
        guard let rate = data.u32(at: offset),
              let inputChannels = data.u32(at: offset + 4),
              let outputChannels = data.u32(at: offset + 8),
              let inputRaw = data.u8(at: offset + 12),
              let outputRaw = data.u8(at: offset + 13),
              let input = AudioOpticalMode(rawValue: inputRaw),
              let output = AudioOpticalMode(rawValue: outputRaw) else { return nil }
        return AudioConfigurationCapability(
            sampleRateHz: rate,
            inputOptical: input,
            outputOptical: output,
            inputChannels: inputChannels,
            outputChannels: outputChannels
        )
    }
}

private enum AudioControlSurfaceWireDecoder {
    private static let wireSize = 664
    private static let maximumValueCount = 80

    static func decode(_ data: Data) -> AudioControlSurfaceSnapshot? {
        guard data.count == wireSize,
              let version = data.u32(at: 0), version == 2,
              let kind = data.u32(at: 4), kind == 0x4D41_3134,
              let endpointRaw = data.u64(at: 8), endpointRaw != 0,
              let revision = data.u32(at: 16),
              let count = data.u32(at: 20), count <= maximumValueCount else {
            return nil
        }
        let values = (0..<Int(count)).compactMap { index -> AudioControlSurfaceValue? in
            let offset = 24 + index * 8
            guard let id = data.u32(at: offset), let value = data.i32(at: offset + 4) else {
                return nil
            }
            return AudioControlSurfaceValue(id: id, value: value)
        }
        guard values.count == Int(count) else { return nil }
        return AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(rawValue: endpointRaw), kind: kind,
            revision: revision, values: values)
    }
}

/// Mirrors the compact scalar layout of selectors 1023–1025. The layout is
/// deliberately fixed-size and bounded, so the async completion queue never
/// transports a pointer or allocates a variable-size reply.
private enum AudioAsyncSnapshotDecoder {
    static func configuration(_ values: [UInt64]) -> AudioConfigurationSnapshot? {
        guard values.count >= 4, values[1] != 0,
              let committed = capability(values[2]) else { return nil }
        let count = Int(values[3])
        guard count <= 8, values.count == 4 + count else { return nil }
        let capabilities = values.dropFirst(4).compactMap { capability($0) }
        guard capabilities.count == count else { return nil }
        return AudioConfigurationSnapshot(
            endpointID: AudioEndpointID(rawValue: values[1]),
            committed: committed, capabilities: capabilities)
    }

    struct ControlsHeader {
        let endpointID: AudioEndpointID
        let kind: UInt32
        let revision: UInt32
        let valueCount: Int
    }

    /// The async control-surface reply is a header only — see
    /// `HandleGetAudioControlSurfaceAsync`. Values come from the struct selector.
    static func controlsHeader(_ values: [UInt64]) -> ControlsHeader? {
        guard values.count == 4, values[1] != 0 else { return nil }
        let header = values[2]
        return ControlsHeader(
            endpointID: AudioEndpointID(rawValue: values[1]),
            kind: UInt32(truncatingIfNeeded: header >> 32),
            revision: UInt32(truncatingIfNeeded: header),
            valueCount: Int(values[3]))
    }

    static func meters(_ values: [UInt64]) -> AudioMeterSnapshot? {
        guard values.count >= 4, values[1] != 0 else { return nil }
        let rateAndRevision = values[2]
        let flagsAndCount = values[3]
        let revision = UInt32(truncatingIfNeeded: rateAndRevision)
        let rate = UInt32(truncatingIfNeeded: rateAndRevision >> 32)
        let count = Int(UInt32(truncatingIfNeeded: flagsAndCount))
        let enabled = ((flagsAndCount >> 32) & 1) != 0
        let locked = ((flagsAndCount >> 33) & 1) != 0
        let external = ((flagsAndCount >> 34) & 1) != 0
        let hardwareSwitch = ((flagsAndCount >> 35) & 1) != 0
        let rotaryCount = Int((flagsAndCount >> 36) & 0xFF)
        let quadletCount = (count + 3) / 4
        // One trailing scalar carries the encoders, packed four to a scalar.
        guard count <= 40, rotaryCount <= 3, values.count == 4 + quadletCount + 1 else {
            return nil
        }
        var peaks: [Int16] = []
        peaks.reserveCapacity(count)
        for index in 0..<count {
            let packed = values[4 + index / 4]
            let raw = UInt16(truncatingIfNeeded: packed >> ((index % 4) * 16))
            peaks.append(Int16(bitPattern: raw))
        }
        let packedRotaries = values[4 + quadletCount]
        let rotaries = (0..<rotaryCount).map { index -> Int16 in
            Int16(bitPattern: UInt16(truncatingIfNeeded: packedRotaries >> (index * 16)))
        }
        return AudioMeterSnapshot(
            endpointID: AudioEndpointID(rawValue: values[1]), revision: revision,
            detectedSampleRateHz: rate, isEnabled: enabled, isClockLocked: locked,
            isExternallySynced: external, hardwareSwitch: hardwareSwitch,
            rotaries: rotaries, values: peaks)
    }

    private static func capability(_ packed: UInt64) -> AudioConfigurationCapability? {
        let rate = UInt32(truncatingIfNeeded: packed)
        let inputChannels = UInt32((packed >> 32) & 0xff)
        let outputChannels = UInt32((packed >> 40) & 0xff)
        guard rate != 0,
              let input = AudioOpticalMode(rawValue: UInt8((packed >> 48) & 0xff)),
              let output = AudioOpticalMode(rawValue: UInt8((packed >> 56) & 0xff)) else {
            return nil
        }
        return AudioConfigurationCapability(
            sampleRateHz: rate, inputOptical: input, outputOptical: output,
            inputChannels: inputChannels, outputChannels: outputChannels)
    }
}

private extension Data {
    func u8(at offset: Int) -> UInt8? {
        guard offset < count else { return nil }
        return self[startIndex + offset]
    }

    func u32(at offset: Int) -> UInt32? {
        guard offset >= 0, offset + 4 <= count else { return nil }
        return withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self) }
    }

    func u64(at offset: Int) -> UInt64? {
        guard offset >= 0, offset + 8 <= count else { return nil }
        return withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt64.self) }
    }

    func i32(at offset: Int) -> Int32? {
        guard let value = u32(at: offset) else { return nil }
        return Int32(bitPattern: value)
    }
}
