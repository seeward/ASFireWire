import CoreAudio
import Foundation

enum ADKLiveCommands {
    private struct Profile {
        let slot: Int
        let uid: String
        let name: String
    }

    private static let definitions: [(String, String, ASFWVirtualDeviceKind)] = [
        ("VirtualADKAudioLab.Duet", "Duet", ASFW_VIRTUAL_DEVICE_DUET),
        ("VirtualADKAudioLab.Phase88", "PHASE 88", ASFW_VIRTUAL_DEVICE_PHASE88),
        ("VirtualADKAudioLab.FW1814", "FireWire 1814", ASFW_VIRTUAL_DEVICE_FW1814),
        ("VirtualADKAudioLab.Saffire", "Saffire Pro 24 DSP", ASFW_VIRTUAL_DEVICE_SAFFIRE_PRO24_DSP),
    ]

    static func runIfRequested(_ arguments: [String]) -> Int32? {
        guard let first = arguments.first else { return nil }
        if first == "--adk-status" {
            return status()
        }
        if first == "--adk-smoke" {
            return smoke()
        }
        guard first == "adk" else { return nil }

        switch arguments.dropFirst().first {
        case "status":
            return status()
        case "events":
            if arguments.count == 2 {
                return dumpEvents(slot: nil)
            }
            guard arguments.count == 3,
                  let slot = Int(arguments[2]),
                  definitions.indices.contains(slot) else {
                printUsage()
                return 64
            }
            return dumpEvents(slot: slot)
        case "smoke":
            return smoke()
        case "rate":
            guard arguments.count == 4,
                  let slot = Int(arguments[2]),
                  let rate = UInt32(arguments[3]) else {
                printUsage()
                return 64
            }
            return setRate(slot: slot, rate: rate)
        case "hal-rate":
            guard arguments.count == 4,
                  let slot = Int(arguments[2]),
                  let rate = UInt32(arguments[3]) else {
                printUsage()
                return 64
            }
            return setHALRate(slot: slot, rate: rate)
        case "config":
            guard arguments.count == 6,
                  let slot = Int(arguments[2]),
                  let rate = UInt32(arguments[3]),
                  let opticalInput = opticalMode(arguments[4]),
                  let opticalOutput = opticalMode(arguments[5]) else {
                printUsage()
                return 64
            }
            return setConfiguration(slot: slot, rate: rate,
                                    opticalInput: opticalInput,
                                    opticalOutput: opticalOutput)
        default:
            printUsage()
            return 64
        }
    }

    private static func expectedProfiles() -> [Profile] {
        definitions.enumerated().map { slot, definition in
            return Profile(
                slot: slot,
                uid: definition.0,
                name: definition.1)
        }
    }

    private static func status() -> Int32 {
        let profiles = expectedProfiles()
        let hal = CoreAudioLabSnapshot.capture()
        let client = ADKConfigClient()
        var failures = profiles.count == definitions.count ? 0 : 1

        print("ADK live status — model ↔ CoreAudio HAL ↔ dext")
        for profile in profiles {
            guard let device = hal.first(where: { $0.uid == profile.uid }) else {
                print("FAIL slot \(profile.slot) \(profile.name): missing from CoreAudio")
                failures += 1
                continue
            }

            do {
                let state = try client.state(slot: profile.slot)
                let halRate = UInt32(device.nominalSampleRate)
                let shapeOK = device.inputChannels == state.currentInputChannels &&
                    device.outputChannels == state.currentOutputChannels
                let rateOK = state.currentSampleRate == halRate &&
                    !state.configurationPending &&
                    (halRate == 44_100 || halRate == 48_000)
                let healthy = shapeOK && rateOK
                let marker = healthy ? "PASS" : "FAIL"
                print("\(marker) slot \(profile.slot) \(profile.name): " +
                      "dext=\(state.currentInputChannels)in/\(state.currentOutputChannels)out " +
                      "hal=\(device.inputChannels)in/\(device.outputChannels)out " +
                      "halRate=\(halRate) " +
                      "dextRate=\(state.currentSampleRate) pending=\(state.configurationPending) " +
                      "running=\(device.isRunning)")
                if !healthy { failures += 1 }
            } catch {
                print("FAIL slot \(profile.slot) \(profile.name): \(error.localizedDescription)")
                failures += 1
            }
        }
        return failures == 0 ? 0 : 1
    }

    private static func opticalMode(_ value: String) -> UInt32? {
        switch value.lowercased() {
        case "none": return 0
        case "adat": return 1
        case "spdif", "s/pdif": return 2
        default: return nil
        }
    }

    private static func setRate(slot: Int, rate: UInt32) -> Int32 {
        guard definitions.indices.contains(slot), rate == 44_100 || rate == 48_000 else {
            printUsage()
            return 64
        }
        guard let profile = expectedProfiles().first(where: { $0.slot == slot }) else {
            print("FAIL: model profile for slot \(slot) is unavailable")
            return 1
        }
        let client = ADKConfigClient()
        do {
            let before = try client.state(slot: slot)
            try client.requestSampleRate(slot: slot, rate: rate)
            let result = try waitForRate(
                client: client, profile: profile, rate: rate,
                firstSequence: before.nextSequence)
            printTransaction(
                profile: profile,
                firstSequence: before.nextSequence, result: result)
            return result.converged && result.hasRequiredPhases ? 0 : 1
        } catch {
            print("FAIL slot \(slot) rate \(rate): \(error.localizedDescription)")
            return 1
        }
    }

    private static func setConfiguration(slot: Int, rate: UInt32,
                                         opticalInput: UInt32,
                                         opticalOutput: UInt32) -> Int32 {
        guard definitions.indices.contains(slot), rate == 44_100 || rate == 48_000 else {
            printUsage()
            return 64
        }
        guard let profile = expectedProfiles().first(where: { $0.slot == slot }) else {
            print("FAIL: profile for slot \(slot) is unavailable")
            return 1
        }

        let client = ADKConfigClient()
        do {
            let before = try client.state(slot: slot)
            try client.requestConfiguration(
                slot: slot, rate: rate, opticalInput: opticalInput,
                opticalOutput: opticalOutput)
            let result = try waitForConfiguration(
                client: client, profile: profile, rate: rate,
                opticalInput: opticalInput, opticalOutput: opticalOutput,
                firstSequence: before.nextSequence)
            printTransaction(profile: profile, firstSequence: before.nextSequence,
                             result: result)
            return result.converged && result.hasRequiredPhases ? 0 : 1
        } catch {
            print("FAIL slot \(slot) configuration: \(error.localizedDescription)")
            return 1
        }
    }

    /// Sets CoreAudio's nominal-rate property directly, bypassing the lab
    /// diagnostic user client. This exercises the same device callback path as
    /// Audio MIDI Setup and a DAW: IOUserAudioDevice::HandleChangeSampleRate.
    private static func setHALRate(slot: Int, rate: UInt32) -> Int32 {
        guard definitions.indices.contains(slot), rate == 44_100 || rate == 48_000,
              let profile = expectedProfiles().first(where: { $0.slot == slot }),
              let device = CoreAudioLabSnapshot.capture().first(where: {
                  $0.uid == profile.uid
              }) else {
            printUsage()
            return 64
        }
        guard !device.isRunning else {
            print("FAIL slot \(slot) \(profile.name): device is running; refusing HAL rate change")
            return 1
        }

        let client = ADKConfigClient()
        do {
            let before = try client.state(slot: slot)
            var coreAudioNotificationObserved = false
            let observer = CoreAudioLabObserver {
                coreAudioNotificationObserved = true
            }
            observer.start()
            defer { observer.stop() }

            var requestedRate = Double(rate)
            var address = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyNominalSampleRate,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain)
            let status = AudioObjectSetPropertyData(
                device.id, &address, 0, nil,
                UInt32(MemoryLayout<Double>.size), &requestedRate)
            guard status == noErr else {
                print(String(format: "FAIL slot %d HAL rate %u: AudioObjectSetPropertyData=0x%08x",
                             slot, rate, UInt32(bitPattern: status)))
                return 1
            }

            let result = try waitForRate(
                client: client, profile: profile, rate: rate,
                firstSequence: before.nextSequence)
            let phases = Set(result.events.filter {
                $0.sequence >= before.nextSequence
            }.map(\.phase))
            let deadline = Date().addingTimeInterval(1.0)
            while !coreAudioNotificationObserved && Date() < deadline {
                RunLoop.main.run(
                    mode: .default,
                    before: Date().addingTimeInterval(0.02))
            }
            let handleChangeObserved = phases.contains(16) && phases.contains(17)
            let passed = result.converged && coreAudioNotificationObserved
            print("\(passed ? "PASS" : "FAIL") external HAL slot \(slot) \(profile.name): " +
                  "dext=\(result.state.currentSampleRate) hal=\(UInt32(result.hal?.nominalSampleRate ?? 0)) " +
                  "CoreAudioListener=\(coreAudioNotificationObserved ? "observed" : "missing") " +
                  "HandleChangeSampleRate=\(handleChangeObserved ? "observed" : "not-called")")
            for event in result.events where event.sequence >= before.nextSequence {
                print("  \(eventLine(profile: profile, event: event))")
            }
            return passed ? 0 : 1
        } catch {
            print("FAIL slot \(slot) HAL rate \(rate): \(error.localizedDescription)")
            return 1
        }
    }

    private static func dumpEvents(slot: Int?) -> Int32 {
        let profiles = expectedProfiles().filter { slot == nil || $0.slot == slot }
        let client = ADKConfigClient()
        var failed = profiles.isEmpty

        print("ADK configuration event ring — read only")
        for profile in profiles {
            do {
                let state = try client.state(slot: profile.slot)
                let events = try client.events(
                    slot: profile.slot, maxEvents: ADKConfigWire.maxEvents)
                print("slot \(profile.slot) \(profile.name): " +
                      "rate=\(state.currentSampleRate) " +
                      "pending=\(state.configurationPending) events=\(events.count)")
                for event in events {
                    let line = eventLine(profile: profile, event: event)
                    print("  \(line)")
                    ADKConfigTrace.emit(line)
                }
            } catch {
                print("FAIL slot \(profile.slot) \(profile.name): " +
                      error.localizedDescription)
                failed = true
            }
        }
        return failed ? 1 : 0
    }

    private static func smoke() -> Int32 {
        let profiles = expectedProfiles()
        var failed = status() != 0
        let client = ADKConfigClient()

        print("\nADK request/perform/abort smoke — alternate then restore")
        for profile in profiles {
            guard let hal = CoreAudioLabSnapshot.capture().first(
                where: { $0.uid == profile.uid }) else {
                print("FAIL slot \(profile.slot): CoreAudio device disappeared")
                failed = true
                continue
            }
            guard !hal.isRunning else {
                print("FAIL slot \(profile.slot): device is running; refusing to alter rate")
                failed = true
                continue
            }

            do {
                let original = try client.state(slot: profile.slot).currentSampleRate
                let alternate: UInt32 = original == 48_000 ? 44_100 : 48_000
                let firstSequence = try client.state(slot: profile.slot).nextSequence

                try client.requestSampleRate(slot: profile.slot, rate: alternate)
                let changed = try waitForRate(
                    client: client, profile: profile, rate: alternate,
                    firstSequence: firstSequence)
                printTransaction(
                    profile: profile,
                    firstSequence: firstSequence, result: changed)
                if !changed.converged || !changed.hasRequiredPhases {
                    failed = true
                }

                let restoreSequence = try client.state(slot: profile.slot).nextSequence
                try client.requestSampleRate(slot: profile.slot, rate: original)
                let restored = try waitForRate(
                    client: client, profile: profile, rate: original,
                    firstSequence: restoreSequence)
                printTransaction(
                    profile: profile,
                    firstSequence: restoreSequence, result: restored)
                if !restored.converged || !restored.hasRequiredPhases {
                    failed = true
                }
            } catch {
                print("FAIL slot \(profile.slot) \(profile.name): \(error.localizedDescription)")
                failed = true
            }
        }

        print("\nADK smoke: \(failed ? "FAIL" : "PASS")")
        return failed ? 1 : 0
    }

    private struct RateResult {
        let state: ADKConfigState
        let hal: CoreAudioLabDevice?
        let events: [ADKConfigEvent]
        let converged: Bool
        let hasRequiredPhases: Bool
    }

    private static func waitForConfiguration(
        client: ADKConfigClient,
        profile: Profile,
        rate: UInt32,
        opticalInput: UInt32,
        opticalOutput: UInt32,
        firstSequence: UInt64
    ) throws -> RateResult {
        let deadline = Date().addingTimeInterval(3.0)
        var state = try client.state(slot: profile.slot)
        var device = CoreAudioLabSnapshot.capture().first { $0.uid == profile.uid }

        repeat {
            let converged = state.currentSampleRate == rate &&
                state.currentOpticalInput == opticalInput &&
                state.currentOpticalOutput == opticalOutput &&
                !state.configurationPending &&
                UInt32(device?.nominalSampleRate ?? 0) == rate &&
                device?.inputChannels == state.currentInputChannels &&
                device?.outputChannels == state.currentOutputChannels
            if converged { break }
            Thread.sleep(forTimeInterval: 0.05)
            state = try client.state(slot: profile.slot)
            device = CoreAudioLabSnapshot.capture().first { $0.uid == profile.uid }
        } while Date() < deadline

        let events = try client.events(slot: profile.slot, maxEvents: 48)
        let phases = Set(events.filter { $0.sequence >= firstSequence }.map(\.phase))
        let required: Set<UInt32> = [2, 3, 7, 10, 18, 19, 20]
        let converged = state.currentSampleRate == rate &&
            state.currentOpticalInput == opticalInput &&
            state.currentOpticalOutput == opticalOutput &&
            !state.configurationPending &&
            UInt32(device?.nominalSampleRate ?? 0) == rate &&
            device?.inputChannels == state.currentInputChannels &&
            device?.outputChannels == state.currentOutputChannels
        return RateResult(state: state, hal: device, events: events,
                          converged: converged,
                          hasRequiredPhases: required.isSubset(of: phases))
    }

    private static func waitForRate(
        client: ADKConfigClient,
        profile: Profile,
        rate: UInt32,
        firstSequence: UInt64
    ) throws -> RateResult {
        let deadline = Date().addingTimeInterval(3.0)
        var state = try client.state(slot: profile.slot)
        var device = CoreAudioLabSnapshot.capture().first { $0.uid == profile.uid }

        repeat {
            let converged = state.currentSampleRate == rate &&
                !state.configurationPending &&
                UInt32(device?.nominalSampleRate ?? 0) == rate
            if converged { break }
            Thread.sleep(forTimeInterval: 0.05)
            state = try client.state(slot: profile.slot)
            device = CoreAudioLabSnapshot.capture().first { $0.uid == profile.uid }
        } while Date() < deadline

        let events = try client.events(slot: profile.slot, maxEvents: 48)
        let transactionEvents = events.filter { $0.sequence >= firstSequence }
        let phases = Set(transactionEvents.map(\.phase))
        let required: Set<UInt32> = [2, 3, 7, 10, 18, 19, 20]
        return RateResult(
            state: state,
            hal: device,
            events: events,
            converged: state.currentSampleRate == rate &&
                !state.configurationPending &&
                UInt32(device?.nominalSampleRate ?? 0) == rate,
            hasRequiredPhases: required.isSubset(of: phases))
    }

    private static func printTransaction(
        profile: Profile,
        firstSequence: UInt64,
        result: RateResult
    ) {
        let marker = result.converged && result.hasRequiredPhases ? "PASS" : "FAIL"
        let halShape = result.hal.map {
            "\($0.inputChannels)in/\($0.outputChannels)out"
        } ?? "missing"
        print("\(marker) slot \(profile.slot) \(profile.name): " +
              "dext=\(result.state.currentSampleRate) " +
              "hal=\(UInt32(result.hal?.nominalSampleRate ?? 0)) " +
              "halShape=\(halShape) " +
              "expected=\(result.state.currentInputChannels)in/\(result.state.currentOutputChannels)out " +
              "phases=\(result.hasRequiredPhases ? "complete" : "incomplete")")
        for event in result.events.filter({ $0.sequence >= firstSequence }) {
            let line = eventLine(profile: profile, event: event)
            print("  \(line)")
            ADKConfigTrace.emit(line)
        }
    }

    private static func eventLine(
        profile: Profile,
        event: ADKConfigEvent
    ) -> String {
        "slot=\(profile.slot) seq=\(event.sequence) " +
            "phase=\(event.phaseName) \(event.mutationSummary) " +
            "result=\(event.resultName)"
    }

    private static func printUsage() {
        print("Usage:")
        print("  ADKLabCLI adk status")
        print("  ADKLabCLI adk events [slot 0...3]")
        print("  ADKLabCLI adk rate <slot 0...3> <44100|48000>")
        print("  ADKLabCLI adk hal-rate <slot 0...3> <44100|48000>")
        print("  ADKLabCLI adk config <slot> <44100|48000> <none|adat|spdif> <none|adat|spdif>")
        print("  ADKLabCLI adk smoke")
    }
}
