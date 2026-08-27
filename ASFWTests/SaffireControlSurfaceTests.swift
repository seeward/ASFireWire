import Testing
@testable import ASFW

struct SaffireControlSurfaceTests {
    @Test func projectsOnlyACompleteSaffireReadback() throws {
        var values: [AudioControlSurfaceValue] = [
            .init(id: 0x5350_0001, value: 0), .init(id: 0x5350_0002, value: 1),
            .init(id: 0x5350_0003, value: 1), .init(id: 0x5350_0004, value: 0),
            .init(id: 0x5350_0120, value: 1), .init(id: 0x5350_0121, value: 0),
            .init(id: 0x5350_0230, value: 1), .init(id: 0x5350_0231, value: 0),
        ]
        for index: UInt32 in 0..<6 {
            values.append(.init(id: 0x5350_0100 + index, value: Int32(100 + index)))
            values.append(.init(id: 0x5350_0110 + index, value: index == 1 ? 1 : 0))
        }
        for index: UInt32 in 0..<3 {
            values.append(.init(id: 0x5350_0130 + index, value: 1))
        }
        for index: UInt32 in 0..<2 {
            values.append(.init(id: 0x5350_0200 + index, value: 1))
            values.append(.init(id: 0x5350_0210 + index, value: index == 0 ? 1 : 0))
            values.append(.init(id: 0x5350_0220 + index, value: 0))
        }
        let snapshot = AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(1), kind: .focusriteSPro24Dsp,
            topologyRevision: 1, stateRevision: 3, values: values)

        let surface = try #require(SaffireControlSurface(snapshot))
        #expect(surface.micInputModes == [.line, .instrument])
        #expect(surface.lineInputLevels == [.high, .low])
        #expect(surface.outputPairs[0].leftVolume == 100)
        #expect(surface.outputPairs[0].rightMuted)
        #expect(surface.outputPairs[0].routeSource == .hostPlayback12)
        #expect(surface.globalMute)
        #expect(surface.channelStrips[0].compressorEnabled)
        #expect(!surface.channelStrips[1].compressorEnabled)
    }

    @Test func rejectsAControlSurfaceWithNoSaffireMeaning() {
        let snapshot = AudioControlSurfaceSnapshot(
            endpointID: AudioEndpointID(1), kind: .apogeeDuet,
            topologyRevision: 1, stateRevision: 1, values: [])
        #expect(SaffireControlSurface(snapshot) == nil)
    }
}
