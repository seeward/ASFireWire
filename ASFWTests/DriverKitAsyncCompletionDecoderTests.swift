import Darwin.Mach
import Foundation
import IOKit
import Testing
@testable import ASFW

@MainActor
struct DriverKitAsyncCompletionDecoderTests {
    @Test func preservesCallerMarkerAndPackedArguments() throws {
        let marker: UInt64 = 0x4153_4657_4346_4753
        let status = kern_return_t(kIOReturnNotReady)
        let arguments: [UInt64] = [
            0x1122_3344_5566_7788,
            2,
            48_000,
            0xaabb_ccdd_eeff_0011,
        ]

        let completion = try #require(decode(
            makeCompletion(marker: marker, status: status, arguments: arguments)
        ))

        #expect(completion.reference == marker)
        #expect(completion.status == status)
        #expect(completion.arguments == arguments)
    }

    @Test func reservesReferenceSlotZeroForIOKit() {
        let reference = DriverKitAsyncCompletionDecoder.reference(marker: 0x1234)
        #expect(reference == [0, 0x1234])
    }

    @Test(arguments: [true, false])
    func rejectsWrongMachEnvelope(complex: Bool) {
        var data = makeCompletion(marker: 1, status: KERN_SUCCESS, arguments: [2])
        data.withUnsafeMutableBytes { raw in
            let header = raw.bindMemory(to: mach_msg_header_t.self).baseAddress!
            if complex {
                header.pointee.msgh_bits |= mach_msg_bits_t(MACH_MSGH_BITS_COMPLEX)
            } else {
                header.pointee.msgh_id = 999
            }
        }
        #expect(decode(data) == nil)
    }

    @Test func rejectsContentPastMachMessageBoundary() {
        var data = makeCompletion(marker: 1, status: KERN_SUCCESS, arguments: [2])
        write(UInt32.max, at: MemoryLayout<mach_msg_header_t>.size, into: &data)
        #expect(decode(data) == nil)
    }

    private func decode(
        _ data: Data
    ) -> DriverKitAsyncCompletionDecoder.Completion? {
        data.withUnsafeBytes { DriverKitAsyncCompletionDecoder.decode($0) }
    }

    private func makeCompletion(
        marker: UInt64,
        status: kern_return_t,
        arguments: [UInt64]
    ) -> Data {
        let notificationBytes = MemoryLayout<mach_msg_size_t>.size
            + MemoryLayout<natural_t>.size
            + 8 * MemoryLayout<UInt64>.size
        let contentBytes = MemoryLayout<kern_return_t>.size
            + arguments.count * MemoryLayout<UInt64>.size
        let messageBytes = MemoryLayout<mach_msg_header_t>.size
            + notificationBytes
            + contentBytes
        var data = Data(repeating: 0, count: messageBytes)

        data.withUnsafeMutableBytes { raw in
            let header = raw.bindMemory(to: mach_msg_header_t.self).baseAddress!
            header.pointee.msgh_size = mach_msg_size_t(messageBytes)
            header.pointee.msgh_id = mach_msg_id_t(kOSNotificationMessageID)
        }

        let notificationOffset = MemoryLayout<mach_msg_header_t>.size
        write(mach_msg_size_t(contentBytes), at: notificationOffset, into: &data)
        write(
            natural_t(kIOAsyncCompletionNotificationType),
            at: notificationOffset + MemoryLayout<mach_msg_size_t>.size,
            into: &data
        )
        let referencesOffset = notificationOffset
            + MemoryLayout<mach_msg_size_t>.size
            + MemoryLayout<natural_t>.size
        write(
            marker,
            at: referencesOffset
                + DriverKitAsyncCompletionDecoder.callerReferenceIndex
                * MemoryLayout<UInt64>.size,
            into: &data
        )

        let resultOffset = notificationOffset + notificationBytes
        write(status, at: resultOffset, into: &data)
        for (index, argument) in arguments.enumerated() {
            write(
                argument,
                at: resultOffset
                    + MemoryLayout<kern_return_t>.size
                    + index * MemoryLayout<UInt64>.size,
                into: &data
            )
        }
        return data
    }

    private func write<T: FixedWidthInteger>(
        _ value: T,
        at offset: Int,
        into data: inout Data
    ) {
        var value = value.littleEndian
        withUnsafeBytes(of: &value) { bytes in
            data.replaceSubrange(offset..<offset + bytes.count, with: bytes)
        }
    }
}
