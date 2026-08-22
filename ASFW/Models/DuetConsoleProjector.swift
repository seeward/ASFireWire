import Foundation

/// Projects the driver's Duet semantic graph into the device's fixed console
/// layout.  A graph that is incomplete or belongs to a different device is
/// rejected instead of being rendered as a misleading set of generic knobs.
nonisolated enum DuetConsoleProjector {
    static func make(
        topology: AudioSemanticTopologySnapshot,
        controls: AudioControlSurfaceSnapshot
    ) -> DuetConsoleSnapshot? {
        guard controls.isSemanticTopologyBacked,
              controls.endpointID == topology.endpointID,
              controls.topologyRevision == topology.topologyRevision else {
            return nil
        }

        let parameters = topology.parameters
        let gains = parameters.filter(isInputGain).sorted { $0.targetID < $1.targetID }
        guard gains.count == 2 else { return nil }

        let inputs = gains.enumerated().compactMap { offset, gain -> DuetConsoleSnapshot.InputStrip? in
            guard let phantom = parameter(.phantomPower, on: gain.targetID, in: parameters),
                  let phase = parameter(.phaseInvert, on: gain.targetID, in: parameters),
                  let nominal = parameter(.nominalLevel, on: gain.targetID, in: parameters),
                  let gainControl = control(for: gain, in: controls),
                  let phantomControl = control(for: phantom, in: controls),
                  let phaseControl = control(for: phase, in: controls),
                  let nominalControl = control(for: nominal, in: controls) else {
                return nil
            }

            return .init(
                id: gain.targetID,
                name: "Input \(offset + 1)",
                gain: gainControl,
                phantomPower: phantomControl,
                phaseInvert: phaseControl,
                nominalLevel: nominalControl
            )
        }
        guard inputs.count == 2 else { return nil }

        let mainLevels = parameters.filter {
            $0.targetKind == .port && $0.kind == .level && $0.unit == .decibels && $0.minimum < 0
        }
        guard mainLevels.count == 1,
              let mute = parameter(.mute, on: mainLevels[0].targetID, in: parameters),
              let outputLevel = control(for: mainLevels[0], in: controls),
              let outputMute = control(for: mute, in: controls) else {
            return nil
        }

        let sourceIDs = Array(Set(topology.crosspoints.map(\.sourcePortID))).sorted()
        let destinationIDs = Array(Set(topology.crosspoints.map(\.destinationPortID))).sorted()
        guard sourceIDs.count == 4, destinationIDs.count == 2 else { return nil }

        let sourceNames = ["Input 1", "Input 2", "DAW Playback 1", "DAW Playback 2"]
        let destinationNames = ["Mixer L", "Mixer R"]
        let mixerSources = zip(sourceIDs, sourceNames).compactMap { sourceID, name -> DuetConsoleSnapshot.MixerSource? in
            let sends = zip(destinationIDs, destinationNames).compactMap { destinationID, destinationName -> DuetConsoleSnapshot.MixerSend? in
                guard let crosspoint = topology.crosspoints.first(where: {
                    $0.sourcePortID == sourceID && $0.destinationPortID == destinationID
                }),
                let parameter = parameters.first(where: {
                    $0.targetKind == .crosspoint && $0.targetID == crosspoint.id &&
                    $0.kind == .level && $0.unit == .normalized
                }),
                let level = control(for: parameter, in: controls) else {
                    return nil
                }
                return .init(id: crosspoint.id, destinationName: destinationName, level: level)
            }
            guard sends.count == 2 else { return nil }
            return .init(id: sourceID, name: name, sends: sends)
        }
        guard mixerSources.count == 4 else { return nil }

        return .init(
            inputs: inputs,
            mixerSources: mixerSources,
            mainOutput: .init(level: outputLevel, mute: outputMute)
        )
    }

    private static func isInputGain(_ parameter: AudioSemanticTopologySnapshot.Parameter) -> Bool {
        parameter.targetKind == .port &&
            parameter.kind == .level &&
            parameter.unit == .decibels &&
            parameter.minimum >= 0
    }

    private static func parameter(
        _ kind: AudioSemanticTopologySnapshot.ParameterKind,
        on portID: UInt32,
        in parameters: [AudioSemanticTopologySnapshot.Parameter]
    ) -> AudioSemanticTopologySnapshot.Parameter? {
        parameters.first { $0.targetKind == .port && $0.targetID == portID && $0.kind == kind }
    }

    private static func control(
        for parameter: AudioSemanticTopologySnapshot.Parameter,
        in controls: AudioControlSurfaceSnapshot
    ) -> DuetConsoleSnapshot.Control? {
        guard let value = controls.values.first(where: { $0.id == parameter.id })?.value else { return nil }
        return .init(parameter: parameter, value: value)
    }
}
