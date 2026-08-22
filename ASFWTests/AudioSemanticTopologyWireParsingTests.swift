import Foundation
import Testing
@testable import ASFW

struct AudioSemanticTopologyWireParsingTests {
    private func setLE<T: FixedWidthInteger>(_ value: T, at offset: Int, in data: inout Data) {
        var raw = value.littleEndian
        withUnsafeBytes(of: &raw) { bytes in
            data.replaceSubrange(offset..<(offset + bytes.count), with: bytes)
        }
    }

    private func fixture() -> Data {
        var wire = Data(repeating: 0, count: 3944)
        setLE(UInt32(3), at: 0, in: &wire)
        setLE(UInt64(71), at: 8, in: &wire)
        setLE(UInt32(3), at: 16, in: &wire)
        setLE(UInt32(0x4455_4554), at: 20, in: &wire)
        setLE(UInt64(12), at: 24, in: &wire)
        setLE(UInt32(1), at: 32, in: &wire) // nodes
        setLE(UInt32(1), at: 36, in: &wire) // ports
        setLE(UInt32(1), at: 56, in: &wire) // crosspoints
        setLE(UInt32(1), at: 60, in: &wire) // parameters
        setLE(UInt32(1), at: 64, in: &wire) // meters

        setLE(UInt32(1), at: 68, in: &wire)
        setLE(UInt32(1), at: 72, in: &wire)
        setLE(UInt32(1), at: 76, in: &wire)

        setLE(UInt32(1), at: 260, in: &wire)
        setLE(UInt32(1), at: 264, in: &wire)
        setLE(UInt32(2), at: 268, in: &wire)
        setLE(UInt32(1), at: 272, in: &wire)
        setLE(UInt32(1), at: 276, in: &wire)

        setLE(UInt32(7), at: 1956, in: &wire)
        setLE(UInt32(43), at: 1960, in: &wire)
        setLE(UInt32(46), at: 1964, in: &wire)
        setLE(UInt32(1), at: 1968, in: &wire) // primary fader
        setLE(UInt32(2), at: 1972, in: &wire) // DAW group
        setLE(UInt32(1), at: 1976, in: &wire) // right channel

        setLE(UInt32(9), at: 2532, in: &wire)
        setLE(UInt32(1), at: 2536, in: &wire)
        setLE(UInt32(1), at: 2540, in: &wire)
        setLE(UInt32(1), at: 2544, in: &wire)
        setLE(UInt32(2), at: 2548, in: &wire)
        setLE(UInt32(1), at: 2552, in: &wire)
        setLE(Int32(-64), at: 2556, in: &wire)
        setLE(Int32(0), at: 2560, in: &wire)
        setLE(Int32(1), at: 2564, in: &wire)
        setLE(UInt32(2), at: 2568, in: &wire)

        setLE(UInt32(1), at: 3652, in: &wire)
        setLE(UInt32(1), at: 3656, in: &wire)
        setLE(UInt32(1), at: 3660, in: &wire)
        setLE(UInt32(0), at: 3664, in: &wire)
        setLE(Int32(0), at: 3668, in: &wire)
        setLE(Int32.max, at: 3672, in: &wire)
        return wire
    }

    @Test func decodesTheDriverPinnedTopologyLayout() throws {
        let snapshot = try #require(AudioSemanticTopologyWireDecoder.decode(fixture()))

        #expect(snapshot.endpointID == AudioEndpointID(71))
        #expect(snapshot.deviceKind == 0x4455_4554)
        #expect(snapshot.topologyRevision == 12)
        #expect(snapshot.nodes.count == 1)
        #expect(snapshot.ports.first?.signalKind == .analogMicXlr)
        #expect(snapshot.crosspoints.first?.presentation == .primaryFader)
        #expect(snapshot.crosspoints.first?.presentationGroup == .hostPlayback)
        #expect(snapshot.crosspoints.first?.presentationOrder == 1)
        #expect(snapshot.parameters.first?.minimum == -64)
        #expect(snapshot.parameters.first?.presentation == .fader)
        #expect(snapshot.meters.first?.maximum == .max)
    }

    @Test func rejectsUnknownParameterDomain() {
        var invalid = fixture()
        setLE(UInt32(99), at: 2552, in: &invalid)
        #expect(AudioSemanticTopologyWireDecoder.decode(invalid) == nil)
    }

    @Test func decodesSemanticTopologyEndpointDiscovery() {
        var wire = Data(repeating: 0, count: 72)
        setLE(UInt32(1), at: 0, in: &wire)
        setLE(UInt32(2), at: 4, in: &wire)
        setLE(UInt64(7), at: 8, in: &wire)
        setLE(UInt64(11), at: 16, in: &wire)

        #expect(AudioSemanticTopologyWireDecoder.decodeEndpointIDs(wire) == [
            AudioEndpointID(7), AudioEndpointID(11),
        ])
    }
}
