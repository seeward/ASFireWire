import CoreAudio
import Foundation

/// Observes CoreAudio-owned state. This is deliberately separate from the
/// diagnostic user-client: Audio MIDI Setup and DAWs bypass that client, but
/// they still cause CoreAudio property notifications.
final class CoreAudioLabObserver {
    private struct Registration {
        let objectID: AudioObjectID
        let address: AudioObjectPropertyAddress
        let block: AudioObjectPropertyListenerBlock
    }

    private let callbackQueue = DispatchQueue(
        label: "net.mrmidi.ASFW.ADKLab.coreaudio-observer")
    private let onChange: () -> Void
    private var registrations: [Registration] = []
    private var isObserving = false

    init(onChange: @escaping () -> Void) {
        self.onChange = onChange
    }

    deinit { stop() }

    func start() {
        guard !isObserving else { return }
        isObserving = true
        addSystemDeviceListListener()
        reconcileDevices()
    }

    func stop() {
        guard isObserving else { return }
        for registration in registrations {
            var address = registration.address
            AudioObjectRemovePropertyListenerBlock(
                registration.objectID, &address, callbackQueue,
                registration.block)
        }
        registrations.removeAll()
        isObserving = false
    }

    private func addSystemDeviceListListener() {
        let address = propertyAddress(kAudioHardwarePropertyDevices)
        let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.reconcileDevices()
                self.onChange()
            }
        }
        add(objectID: AudioObjectID(kAudioObjectSystemObject),
            address: address, block: block)
    }

    private func reconcileDevices() {
        guard isObserving else { return }
        let systemID = AudioObjectID(kAudioObjectSystemObject)
        let deviceRegistrations = registrations.filter {
            $0.objectID != systemID
        }
        remove(deviceRegistrations)
        registrations.removeAll { $0.objectID != systemID }

        for device in CoreAudioLabSnapshot.capture() {
            addDeviceListeners(device)
        }
    }

    private func addDeviceListeners(_ device: CoreAudioLabDevice) {
        addChangeListener(
            objectID: device.id,
            address: propertyAddress(kAudioDevicePropertyNominalSampleRate))

        // A configuration transaction can change channel geometry without
        // changing rate. Stream virtual formats are the CoreAudio truth for
        // that part of the UI, so observe them too.
        for streamID in device.inputStreamIDs + device.outputStreamIDs {
            addChangeListener(
                objectID: streamID,
                address: propertyAddress(kAudioStreamPropertyVirtualFormat))
        }
    }

    private func addChangeListener(objectID: AudioObjectID,
                                   address: AudioObjectPropertyAddress) {
        let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            DispatchQueue.main.async { [weak self] in
                self?.onChange()
            }
        }
        add(objectID: objectID, address: address, block: block)
    }

    private func add(objectID: AudioObjectID,
                     address: AudioObjectPropertyAddress,
                     block: @escaping AudioObjectPropertyListenerBlock) {
        var mutableAddress = address
        guard AudioObjectAddPropertyListenerBlock(
            objectID, &mutableAddress, callbackQueue, block) == noErr else {
            return
        }
        registrations.append(Registration(
            objectID: objectID, address: address, block: block))
    }

    private func remove(_ obsolete: [Registration]) {
        for registration in obsolete {
            var address = registration.address
            AudioObjectRemovePropertyListenerBlock(
                registration.objectID, &address, callbackQueue,
                registration.block)
        }
    }

    private func propertyAddress(
        _ selector: AudioObjectPropertySelector
    ) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
    }
}
