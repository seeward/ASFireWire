import Foundation
import Testing
@testable import ASFW

struct DuetConsoleProjectorTests {
    @Test func projectsTheHintedStereoMixerWithoutPortOrderInference() throws {
        let topology = topologyFixture()
        let controls = AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(71),
            kind: .apogeeDuet,
            topologyRevision: 12,
            stateRevision: 1,
            values: topology.parameters.map { .init(id: $0.id, value: $0.id == 24 ? 0 : Int32($0.id)) }
        )

        let console = try #require(DuetConsoleProjector.make(topology: topology, controls: controls))

        #expect(console.inputs.map { $0.name } == ["Input 1", "Input 2"])
        #expect(console.mixerStrips.map { $0.name } == ["Input Monitor", "DAW Playback"])
        #expect(console.mixerStrips.map { $0.sends.map { $0.destinationName } } == [["L", "R"], ["L", "R"]])
        #expect(console.mixerStrips.map { $0.sends.map { $0.level.value } } == [
            [11, 16], [13, 18],
        ])
        #expect(console.mixerStrips.map { $0.routingSends.map { $0.level.value } } == [
            [12, 15], [14, 17],
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
                parameter(portID + 20, .phaseInvert, .boolean, .none, 0, 1, .toggle, id: portID + 4),
                parameter(portID, .nominalLevel, .enumeration, .none, 0, 2, .selector, id: portID + 6),
            ]
        }
        let outputParameters = [
            parameter(59, .level, .scalar, .decibels, -64, 0, .fader, id: 9),
            parameter(59, .mute, .boolean, .none, 0, 1, .toggle, id: 10),
            parameter(59, .source, .enumeration, .none, 0, 1, .selector, id: 21),
            parameter(59, .nominalLevel, .enumeration, .none, 0, 1, .selector, id: 22),
            parameter(61, .muteFollow, .enumeration, .none, 0, 2, .selector, id: 25),
            parameter(63, .muteFollow, .enumeration, .none, 0, 2, .selector, id: 26),
        ]
        let crosspoints = [
            crosspoint(1, 41, 45, .primaryFader, .inputMonitor, 0), crosspoint(2, 42, 45, .routingFader, .inputMonitor, 0),
            crosspoint(3, 43, 45, .primaryFader, .hostPlayback, 0), crosspoint(4, 44, 45, .routingFader, .hostPlayback, 0),
            crosspoint(5, 41, 46, .routingFader, .inputMonitor, 1), crosspoint(6, 42, 46, .primaryFader, .inputMonitor, 1),
            crosspoint(7, 43, 46, .routingFader, .hostPlayback, 1), crosspoint(8, 44, 46, .primaryFader, .hostPlayback, 1),
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
            parameters: inputParameters + [
                parameter(21, .source, .enumeration, .none, 0, 1, .selector, id: 19),
                parameter(22, .source, .enumeration, .none, 0, 1, .selector, id: 20),
            ] + outputParameters + mixerParameters + [
                .init(id: 23, targetKind: .device, targetID: 1, kind: .stereoLink,
                      valueKind: .boolean, unit: .none, minimum: 0, maximum: 1, step: 1, presentation: .toggle),
                .init(id: 24, targetKind: .device, targetID: 1, kind: .hardwareControlTarget,
                      valueKind: .enumeration, unit: .none, minimum: 0, maximum: 2, step: 1, presentation: .selector),
            ],
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

    private func crosspoint(
        _ id: UInt32, _ source: UInt32, _ destination: UInt32,
        _ presentation: AudioSemanticTopologySnapshot.CrosspointPresentation,
        _ group: AudioSemanticTopologySnapshot.CrosspointGroup, _ order: UInt32
    ) -> AudioSemanticTopologySnapshot.Crosspoint {
        .init(id: id, sourcePortID: source, destinationPortID: destination,
              presentation: presentation, presentationGroup: group, presentationOrder: order)
    }
}
