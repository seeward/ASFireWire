import Foundation

/// Projects the driver's Duet graph into one hardware-console row. The graph
/// remains authoritative; this only chooses the active source-dependent input
/// presentation.
nonisolated enum DuetConsoleProjector {
    static func make(topology: AudioSemanticTopologySnapshot,
                     controls: AudioControlSurfaceSnapshot,
                     meters: AudioMeterSnapshot? = nil) -> DuetConsoleSnapshot? {
        guard controls.isSemanticTopologyBacked,
              controls.endpointID == topology.endpointID,
              controls.topologyRevision == topology.topologyRevision else { return nil }
        let p = topology.parameters
        guard let gain1 = parameter(.level, target: 21, in: p),
              let gain2 = parameter(.level, target: 22, in: p),
              let source1 = parameter(.source, target: 21, in: p),
              let source2 = parameter(.source, target: 22, in: p),
              let phantom1 = parameter(.phantomPower, target: 1, in: p),
              let phantom2 = parameter(.phantomPower, target: 2, in: p),
              let phase1 = parameter(.phaseInvert, target: 21, in: p),
              let phase2 = parameter(.phaseInvert, target: 22, in: p),
              let nominal1 = parameter(.nominalLevel, target: 1, in: p),
              let nominal2 = parameter(.nominalLevel, target: 2, in: p),
              let outputLevel = parameter(.level, target: 59, in: p),
              let outputMute = parameter(.mute, target: 59, in: p),
              let outputSource = parameter(.source, target: 59, in: p),
              let outputNominal = parameter(.nominalLevel, target: 59, in: p),
              let mainMuteFollow = parameter(.muteFollow, target: 61, in: p),
              let headphoneMuteFollow = parameter(.muteFollow, target: 63, in: p),
              let link = deviceParameter(.stereoLink, in: p),
              let hardwareTarget = deviceParameter(.hardwareControlTarget, in: p),
              let outputLevelControl = control(for: outputLevel, in: controls),
              let outputMuteControl = control(for: outputMute, in: controls),
              let outputSourceControl = control(for: outputSource, in: controls),
              let outputNominalControl = control(for: outputNominal, in: controls),
              let mainMuteFollowControl = control(for: mainMuteFollow, in: controls),
              let headphoneMuteFollowControl = control(for: headphoneMuteFollow, in: controls),
              let stereoLink = control(for: link, in: controls),
              let targetControl = control(for: hardwareTarget, in: controls),
              let target = DuetConsoleSnapshot.HardwareTarget(rawValue: targetControl.value) else { return nil }

        let values = meters?.isEnabled == true && meters?.topologyRevision == topology.topologyRevision
            ? meters?.values ?? [] : []
        let inputs = [
            input(name: "Input 1", id: 21, gain: gain1, source: source1, phantom: phantom1,
                  phase: phase1, nominal: nominal1, meter: values[safe: 0] ?? 0,
                  selected: target == .input1, controls: controls),
            input(name: "Input 2", id: 22, gain: gain2, source: source2, phantom: phantom2,
                  phase: phase2, nominal: nominal2, meter: values[safe: 1] ?? 0,
                  selected: target == .input2, controls: controls),
        ].compactMap { $0 }
        guard inputs.count == 2 else { return nil }

        let mixerStrips = mixerStrips(topology: topology, parameters: p, controls: controls, meters: values)
        guard mixerStrips.count == 2 else { return nil }

        let mainOutputMeters: [Int16]
        switch outputSourceControl.value {
        case 0: // Host playback is wired directly to Main Out.
            mainOutputMeters = [values[safe: 2] ?? 0, values[safe: 3] ?? 0]
        case 1: // The cue mix's stereo output is wired to Main Out.
            mainOutputMeters = [values[safe: 4] ?? 0, values[safe: 5] ?? 0]
        default:
            // Keep the control surface available while a malformed or stale
            // status snapshot settles; the driver will reject such a write.
            mainOutputMeters = [0, 0]
        }

        return .init(inputs: inputs, mixerStrips: mixerStrips,
                     mainOutput: .init(level: outputLevelControl, mute: outputMuteControl,
                                       source: outputSourceControl, nominalLevel: outputNominalControl,
                                       mainMuteFollow: mainMuteFollowControl,
                                       headphoneMuteFollow: headphoneMuteFollowControl,
                                       meterLevels: mainOutputMeters,
                                       hardwareSelected: target == .mainOutput),
                     stereoLink: stereoLink, hardwareTarget: target,
                     meteringEnabled: meters?.isEnabled == true)
    }

    private static func input(name: String, id: UInt32,
                              gain: AudioSemanticTopologySnapshot.Parameter,
                              source: AudioSemanticTopologySnapshot.Parameter,
                              phantom: AudioSemanticTopologySnapshot.Parameter,
                              phase: AudioSemanticTopologySnapshot.Parameter,
                              nominal: AudioSemanticTopologySnapshot.Parameter,
                              meter: Int16, selected: Bool,
                              controls: AudioControlSurfaceSnapshot) -> DuetConsoleSnapshot.InputStrip? {
        guard let sourceControl = control(for: source, in: controls),
              let phantomControl = control(for: phantom, in: controls),
              let phaseControl = control(for: phase, in: controls),
              let nominalControl = control(for: nominal, in: controls) else { return nil }
        let activeGain = sourceControl.value == 1 ? adjustedInstrumentGain(gain) : gain
        let gainControl = sourceControl.value == 0 && nominalControl.value != 0
            ? nil : control(for: activeGain, in: controls)
        return .init(id: id, name: name, source: sourceControl, gain: gainControl,
                     phantomPower: phantomControl, phaseInvert: phaseControl,
                     nominalLevel: nominalControl, meterLevel: meter, hardwareSelected: selected)
    }

    private static func mixerStrips(
        topology: AudioSemanticTopologySnapshot,
        parameters: [AudioSemanticTopologySnapshot.Parameter],
        controls: AudioControlSurfaceSnapshot,
        meters: [Int16]
    ) -> [DuetConsoleSnapshot.MixerStrip] {
        let primary = topology.crosspoints.filter { $0.presentation == .primaryFader }
        return Array(Set(primary.map(\.presentationGroup)))
            .sorted { $0.rawValue < $1.rawValue }
            .compactMap { group in
            guard let descriptor = mixerGroupDescriptor(group) else { return nil }
            let primarySends = primary
                .filter { $0.presentationGroup == group }
                .sorted { $0.presentationOrder < $1.presentationOrder }
                .compactMap { mixerSend(for: $0, parameters: parameters, controls: controls) }
            let routingSends = topology.crosspoints
                .filter { $0.presentation == .routingFader && $0.presentationGroup == group }
                .sorted { $0.presentationOrder < $1.presentationOrder }
                .compactMap { mixerSend(for: $0, parameters: parameters, controls: controls) }
            guard primarySends.count == 2, routingSends.count == 2 else { return nil }
            return .init(id: group.rawValue, name: descriptor.name, sends: primarySends,
                         meterLevels: descriptor.meterOffsets.map { meters[safe: $0] ?? 0 },
                         routingSends: routingSends)
        }
    }

    private static func mixerSend(
        for crosspoint: AudioSemanticTopologySnapshot.Crosspoint,
        parameters: [AudioSemanticTopologySnapshot.Parameter],
        controls: AudioControlSurfaceSnapshot
    ) -> DuetConsoleSnapshot.MixerSend? {
        guard let levelParameter = parameters.first(where: {
            $0.targetKind == .crosspoint && $0.targetID == crosspoint.id && $0.kind == .level
        }), let level = control(for: levelParameter, in: controls) else { return nil }
        return .init(id: crosspoint.id,
                     destinationName: crosspoint.presentationOrder == 0 ? "L" : "R",
                     level: level)
    }

    private static func mixerGroupDescriptor(
        _ group: AudioSemanticTopologySnapshot.CrosspointGroup
    ) -> (name: String, meterOffsets: [Int])? {
        // These values are graph-published presentation groups, not inferred
        // from opaque mixer port IDs. The Duet's fixed hardware has one
        // analogue pair and one host-playback pair.
        switch group {
        case .inputMonitor: ("Input Monitor", [0, 1])
        case .hostPlayback: ("DAW Playback", [2, 3])
        case .none: nil
        }
    }

    private static func adjustedInstrumentGain(_ p: AudioSemanticTopologySnapshot.Parameter)
        -> AudioSemanticTopologySnapshot.Parameter {
        .init(id: p.id, targetKind: p.targetKind, targetID: p.targetID, kind: p.kind,
              valueKind: p.valueKind, unit: p.unit, minimum: 0, maximum: 65,
              step: p.step, presentation: p.presentation)
    }

    private static func parameter(_ kind: AudioSemanticTopologySnapshot.ParameterKind,
                                  target: UInt32,
                                  in parameters: [AudioSemanticTopologySnapshot.Parameter])
        -> AudioSemanticTopologySnapshot.Parameter? {
        parameters.first { $0.targetKind == .port && $0.targetID == target && $0.kind == kind }
    }

    private static func deviceParameter(_ kind: AudioSemanticTopologySnapshot.ParameterKind,
                                        in parameters: [AudioSemanticTopologySnapshot.Parameter])
        -> AudioSemanticTopologySnapshot.Parameter? {
        parameters.first { $0.targetKind == .device && $0.kind == kind }
    }

    private static func control(for parameter: AudioSemanticTopologySnapshot.Parameter,
                                in controls: AudioControlSurfaceSnapshot) -> DuetConsoleSnapshot.Control? {
        guard let value = controls.values.first(where: { $0.id == parameter.id })?.value else { return nil }
        return .init(parameter: parameter, value: value)
    }
}

private extension Collection {
    nonisolated subscript(safe index: Index) -> Element? { indices.contains(index) ? self[index] : nil }
}
