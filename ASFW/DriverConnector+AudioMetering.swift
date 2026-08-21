import Foundation
import IOKit

extension ASFWDriverConnector {
    func setAudioMeteringAsync(
        endpointID: AudioEndpointID,
        enabled: Bool,
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
            self.audioMeteringCompletions[requestID] = completion
            var reference = DriverKitAsyncCompletionDecoder.reference(
                marker: Self.audioMeteringAsyncReference
            )
            var inputs = [endpointID.rawValue, enabled ? 1 : 0, requestID]
            let result = IOConnectCallAsyncScalarMethod(
                self.connection, Method.setAudioMeteringEnabledAsync.rawValue,
                self.asyncPort, &reference, UInt32(reference.count),
                &inputs, UInt32(inputs.count), nil, nil)
            if result != KERN_SUCCESS {
                let callback = self.audioMeteringCompletions.removeValue(forKey: requestID)
                DispatchQueue.main.async { callback?(result) }
            }
        }
    }

    func getAudioMeterSnapshot(endpointID: AudioEndpointID) -> AudioMeterSnapshot? {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else { return nil }
        var scalarInput = endpointID.rawValue
        var output = Data(count: 112)
        var outputLength = output.count
        let result = output.withUnsafeMutableBytes { outputBytes in
            IOConnectCallMethod(
                connection,
                Method.getAudioMeterSnapshot.rawValue,
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
        return AudioMeterWireDecoder.decode(output)
    }

    func setAudioMetering(endpointID: AudioEndpointID, enabled: Bool) -> kern_return_t {
        guard isConnected, connection != 0, endpointID.rawValue != 0 else {
            return kIOReturnNotReady
        }
        var scalarInputs = [endpointID.rawValue, enabled ? 1 : 0]
        return IOConnectCallScalarMethod(
            connection,
            Method.setAudioMeteringEnabled.rawValue,
            &scalarInputs,
            UInt32(scalarInputs.count),
            nil,
            nil
        )
    }
}

private enum AudioMeterWireDecoder {
    private static let wireSize = 112
    private static let maximumValueCount = 40

    static func decode(_ data: Data) -> AudioMeterSnapshot? {
        guard data.count == wireSize,
              let version = data.u32(at: 0), version == 1,
              let revision = data.u32(at: 4),
              let endpointRaw = data.u64(at: 8), endpointRaw != 0,
              let count = data.u32(at: 16), count <= maximumValueCount,
              let rate = data.u32(at: 20),
              let enabled = data.u8(at: 24), enabled <= 1,
              let locked = data.u8(at: 25), locked <= 1 else {
            return nil
        }
        let values = (0..<Int(count)).compactMap { data.i16(at: 28 + $0 * 2) }
        guard values.count == Int(count) else { return nil }
        return AudioMeterSnapshot(
            endpointID: AudioEndpointID(rawValue: endpointRaw), revision: revision,
            detectedSampleRateHz: rate, isEnabled: enabled != 0,
            isClockLocked: locked != 0, values: values)
    }
}

private extension Data {
    func u8(at offset: Int) -> UInt8? {
        guard offset >= 0, offset < count else { return nil }
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

    func i16(at offset: Int) -> Int16? {
        guard offset >= 0, offset + 2 <= count else { return nil }
        return withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: Int16.self) }
    }
}
