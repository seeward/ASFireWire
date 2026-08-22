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
        var wire = Data(repeating: 0, count: 3496)
        setLE(UInt32(1), at: 0, in: &wire)
        setLE(UInt64(71), at: 8, in: &wire)
        setLE(UInt32(1), at: 16, in: &wire)
        setLE(UInt32(0x4455_4554), at: 20, in: &wire)
        setLE(UInt64(12), at: 24, in: &wire)
        setLE(UInt32(1), at: 32, in: &wire) // nodes
        setLE(UInt32(1), at: 36, in: &wire) // ports
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

        setLE(UInt32(9), at: 2244, in: &wire)
        setLE(UInt32(1), at: 2248, in: &wire)
        setLE(UInt32(1), at: 2252, in: &wire)
        setLE(UInt32(1), at: 2256, in: &wire)
        setLE(UInt32(2), at: 2260, in: &wire)
        setLE(UInt32(1), at: 2264, in: &wire)
        setLE(Int32(-64), at: 2268, in: &wire)
        setLE(Int32(0), at: 2272, in: &wire)
        setLE(Int32(1), at: 2276, in: &wire)
        setLE(UInt32(2), at: 2280, in: &wire)

        setLE(UInt32(1), at: 3204, in: &wire)
        setLE(UInt32(1), at: 3208, in: &wire)
        setLE(UInt32(1), at: 3212, in: &wire)
        setLE(UInt32(0), at: 3216, in: &wire)
        setLE(Int32(0), at: 3220, in: &wire)
        setLE(Int32.max, at: 3224, in: &wire)
        return wire
    }

    @Test func decodesTheDriverPinnedTopologyLayout() throws {
        let snapshot = try #require(AudioSemanticTopologyWireDecoder.decode(fixture()))

        #expect(snapshot.endpointID == AudioEndpointID(71))
        #expect(snapshot.deviceKind == 0x4455_4554)
        #expect(snapshot.topologyRevision == 12)
        #expect(snapshot.nodes.count == 1)
        #expect(snapshot.ports.first?.signalKind == .analogMicXlr)
        #expect(snapshot.parameters.first?.minimum == -64)
        #expect(snapshot.parameters.first?.presentation == .fader)
        #expect(snapshot.meters.first?.maximum == .max)
    }

    @Test func rejectsUnknownParameterDomain() {
        var invalid = fixture()
        setLE(UInt32(99), at: 2264, in: &invalid)
        #expect(AudioSemanticTopologyWireDecoder.decode(invalid) == nil)
    }
}
