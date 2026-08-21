import Foundation
import IOKit

extension ASFWDriverConnector {
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
}
