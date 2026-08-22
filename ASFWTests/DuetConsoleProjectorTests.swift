import Foundation
import Testing
@testable import ASFW

struct DuetConsoleProjectorTests {
    @Test func projectsTheFourByTwoMixerInWirePortOrder() throws {
        let topology = topologyFixture()
        let controls = AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(71),
            kind: .apogeeDuet,
            topologyRevision: 12,
            stateRevision: 1,
            values: topology.parameters.map { .init(id: $0.id, value: Int32($0.id)) }
        )

        let console = try #require(DuetConsoleProjector.make(topology: topology, controls: controls))

        #expect(console.inputs.map { $0.name } == ["Input 1", "Input 2"])
        #expect(console.mixerSources.map { $0.name } == ["Input 1", "Input 2", "DAW Playback 1", "DAW Playback 2"])
        #expect(console.mixerSources.map { $0.sends.map { $0.destinationName } } == Array(repeating: ["Mixer L", "Mixer R"], count: 4))
        #expect(console.mixerSources.map { $0.sends.map { $0.level.value } } == [
            [11, 15], [12, 16], [13, 17], [14, 18],
        ])
        #expect(console.mainOutput.level.value == 9)
        #expect(console.mainOutput.mute.value == 10)
    }

    @Test func rejectsAnIncompleteMixerGraph() {
        var topology = topologyFixture()
        topology = .init(
            endpointID: topology.endpointID,
            deviceKind: topology.deviceKind,
            topologyRevision: topology.topologyRevision,
            nodes: topology.nodes,
            ports: topology.ports,
            fixedLinks: topology.fixedLinks,
            routers: topology.routers,
            routeBundles: topology.routeBundles,
            routes: topology.routes,
            crosspoints: Array(topology.crosspoints.dropLast()),
            parameters: topology.parameters,
            meters: topology.meters
        )
        let controls = AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(71), kind: .apogeeDuet, topologyRevision: 12, stateRevision: 1,
            values: topology.parameters.map { .init(id: $0.id, value: 0) }
        )

        #expect(DuetConsoleProjector.make(topology: topology, controls: controls) == nil)
    }

    private func topologyFixture() -> AudioSemanticTopologySnapshot {
        let inputParameters = [1, 2].flatMap { portID in
            [
                parameter(portID + 20, .level, .scalar, .decibels, 10, 75, .fader, id: portID),
                parameter(portID, .phantomPower, .boolean, .none, 0, 1, .toggle, id: portID + 2),
                parameter(portID, .phaseInvert, .boolean, .none, 0, 1, .toggle, id: portID + 4),
                parameter(portID, .nominalLevel, .enumeration, .none, 0, 2, .selector, id: portID + 6),
            ]
        }
        let outputParameters = [
            parameter(31, .level, .scalar, .decibels, -64, 0, .fader, id: 9),
            parameter(31, .mute, .boolean, .none, 0, 1, .toggle, id: 10),
        ]
        let crosspoints = [
            crosspoint(1, 41, 45), crosspoint(2, 42, 45), crosspoint(3, 43, 45), crosspoint(4, 44, 45),
            crosspoint(5, 41, 46), crosspoint(6, 42, 46), crosspoint(7, 43, 46), crosspoint(8, 44, 46),
        ]
        let mixerParameters = crosspoints.map { crosspoint in
            AudioSemanticTopologySnapshot.Parameter(
                id: crosspoint.id + 10,
                targetKind: .crosspoint,
                targetID: crosspoint.id,
                kind: .level,
                valueKind: .scalar,
                unit: .normalized,
                minimum: 0,
                maximum: 255,
                step: 1,
                presentation: .fader
            )
        }

        return .init(
            endpointID: AudioEndpointID(71),
            deviceKind: 0x4455_4554,
            topologyRevision: 12,
            nodes: [], ports: [], fixedLinks: [], routers: [], routeBundles: [], routes: [],
            crosspoints: crosspoints,
            parameters: inputParameters + outputParameters + mixerParameters,
            meters: []
        )
    }

    private func parameter(
        _ portID: UInt32,
        _ kind: AudioSemanticTopologySnapshot.ParameterKind,
        _ valueKind: AudioSemanticTopologySnapshot.ValueKind,
        _ unit: AudioSemanticTopologySnapshot.Unit,
        _ minimum: Int32,
        _ maximum: Int32,
        _ presentation: AudioSemanticTopologySnapshot.Presentation,
        id: UInt32
    ) -> AudioSemanticTopologySnapshot.Parameter {
        .init(
            id: id, targetKind: .port, targetID: portID, kind: kind, valueKind: valueKind,
            unit: unit, minimum: minimum, maximum: maximum, step: 1, presentation: presentation
        )
    }

    private func crosspoint(_ id: UInt32, _ source: UInt32, _ destination: UInt32) -> AudioSemanticTopologySnapshot.Crosspoint {
        .init(id: id, sourcePortID: source, destinationPortID: destination)
    }
}
