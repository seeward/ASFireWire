import Foundation
import CoreAudio

struct CoreAudioLabDevice: Identifiable, Sendable {
    let id: AudioObjectID
    let name: String
    let uid: String
    let nominalSampleRate: Double
    let inputStreamIDs: [AudioObjectID]
    let outputStreamIDs: [AudioObjectID]
    let inputChannels: UInt32
    let outputChannels: UInt32
    let isRunning: Bool

    var displayText: String {
        "\(name)  id=0x\(String(id, radix: 16))  rate=\(nominalSampleRate)  " +
            "\(inputChannels)in/\(outputChannels)out  " +
            "streams in=\(inputStreamIDs) out=\(outputStreamIDs)  " +
            "running=\(isRunning)"
    }
}

enum CoreAudioLabSnapshot {
    static func capture() -> [CoreAudioLabDevice] {
        let deviceIDs: [AudioObjectID] = getArray(
            objectID: AudioObjectID(kAudioObjectSystemObject),
            selector: kAudioHardwarePropertyDevices)

        return deviceIDs.compactMap { deviceID in
            let name = getString(objectID: deviceID, selector: kAudioObjectPropertyName)
            guard name.contains("ADK Config Lab") else { return nil }

            let inputStreamIDs: [AudioObjectID] = getArray(
                objectID: deviceID,
                selector: kAudioDevicePropertyStreams,
                scope: kAudioObjectPropertyScopeInput)
            let outputStreamIDs: [AudioObjectID] = getArray(
                objectID: deviceID,
                selector: kAudioDevicePropertyStreams,
                scope: kAudioObjectPropertyScopeOutput)

            return CoreAudioLabDevice(
                id: deviceID,
                name: name,
                uid: getString(objectID: deviceID,
                              selector: kAudioDevicePropertyDeviceUID),
                nominalSampleRate: getValue(
                    objectID: deviceID,
                    selector: kAudioDevicePropertyNominalSampleRate,
                    defaultValue: 0.0),
                inputStreamIDs: inputStreamIDs,
                outputStreamIDs: outputStreamIDs,
                inputChannels: channelCount(streamIDs: inputStreamIDs),
                outputChannels: channelCount(streamIDs: outputStreamIDs),
                isRunning: getValue(
                    objectID: deviceID,
                    selector: kAudioDevicePropertyDeviceIsRunning,
                    defaultValue: UInt32(0)) != 0)
        }
        .sorted { $0.uid < $1.uid }
    }

    private static func channelCount(streamIDs: [AudioObjectID]) -> UInt32 {
        streamIDs.reduce(0) { total, streamID in
            let format: AudioStreamBasicDescription = getValue(
                objectID: streamID,
                selector: kAudioStreamPropertyVirtualFormat,
                defaultValue: AudioStreamBasicDescription())
            return total + format.mChannelsPerFrame
        }
    }

    private static func address(
        selector: AudioObjectPropertySelector,
        scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal
    ) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: scope,
            mElement: kAudioObjectPropertyElementMain)
    }

    private static func getValue<T>(
        objectID: AudioObjectID,
        selector: AudioObjectPropertySelector,
        scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
        defaultValue: T
    ) -> T {
        var propertyAddress = address(selector: selector, scope: scope)
        var size = UInt32(MemoryLayout<T>.size)
        var value = defaultValue
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            AudioObjectGetPropertyData(
                objectID, &propertyAddress, 0, nil, &size, pointer)
        }
        return status == noErr ? value : defaultValue
    }

    private static func getArray<T>(
        objectID: AudioObjectID,
        selector: AudioObjectPropertySelector,
        scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal
    ) -> [T] {
        var propertyAddress = address(selector: selector, scope: scope)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(
            objectID, &propertyAddress, 0, nil, &size) == noErr else {
            return []
        }

        let count = Int(size) / MemoryLayout<T>.size
        guard count > 0 else { return [] }
        var status: OSStatus = noErr
        let result = [T](unsafeUninitializedCapacity: count) { buffer, initializedCount in
            initializedCount = 0
            status = AudioObjectGetPropertyData(
                objectID, &propertyAddress, 0, nil, &size, buffer.baseAddress!)
            if status == noErr {
                initializedCount = Int(size) / MemoryLayout<T>.size
            }
        }
        return status == noErr ? result : []
    }

    private static func getString(
        objectID: AudioObjectID,
        selector: AudioObjectPropertySelector
    ) -> String {
        var propertyAddress = address(selector: selector)
        var stringRef: CFString? = nil
        var size = UInt32(MemoryLayout<CFString?>.size)
        let status = withUnsafeMutablePointer(to: &stringRef) { pointer in
            AudioObjectGetPropertyData(
                objectID, &propertyAddress, 0, nil, &size, pointer)
        }
        guard status == noErr, let stringRef else { return "" }
        return stringRef as String
    }
}
