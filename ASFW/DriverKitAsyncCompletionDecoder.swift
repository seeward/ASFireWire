import Darwin.Mach
import IOKit

/// Decodes the completion messages emitted by DriverKit's
/// `IOUserClient::AsyncCompletion` bridge.
///
/// XNU routes the DriverKit `OSAction` through `sendAsyncResult64()`, which
/// emits a non-complex Mach message containing `OSNotificationHeader64`, a
/// packed `IOReturn`, and up to 16 UInt64 arguments. Async reference slot 0
/// is reserved for the wake port; callers own slots 1...7.
enum DriverKitAsyncCompletionDecoder {
    static let callerReferenceIndex = 1
    static let maximumArgumentCount = 16

    struct Completion: Equatable {
        let reference: UInt64
        let status: kern_return_t
        let arguments: [UInt64]
    }

    static func reference(marker: UInt64) -> [UInt64] {
        [0, marker]
    }

    static func decode(_ raw: UnsafeRawBufferPointer) -> Completion? {
        guard let base = raw.baseAddress,
              raw.count >= MemoryLayout<mach_msg_header_t>.size else {
            return nil
        }

        let message = base.loadUnaligned(as: mach_msg_header_t.self)
        let messageBytes = Int(message.msgh_size)
        guard message.msgh_id == mach_msg_id_t(kOSNotificationMessageID),
              (message.msgh_bits & mach_msg_bits_t(MACH_MSGH_BITS_COMPLEX)) == 0,
              messageBytes >= MemoryLayout<mach_msg_header_t>.size,
              messageBytes <= raw.count else {
            return nil
        }

        let notificationOffset = MemoryLayout<mach_msg_header_t>.size
        let referenceArrayOffset = notificationOffset
            + MemoryLayout<mach_msg_size_t>.size
            + MemoryLayout<natural_t>.size
        let notificationBytes = MemoryLayout<mach_msg_size_t>.size
            + MemoryLayout<natural_t>.size
            + 8 * MemoryLayout<UInt64>.size
        let resultOffset = notificationOffset + notificationBytes

        guard messageBytes >= resultOffset + MemoryLayout<kern_return_t>.size else {
            return nil
        }

        let contentBytes = Int(base.loadUnaligned(
            fromByteOffset: notificationOffset,
            as: mach_msg_size_t.self
        ))
        let notificationType = base.loadUnaligned(
            fromByteOffset: notificationOffset + MemoryLayout<mach_msg_size_t>.size,
            as: natural_t.self
        )
        guard notificationType == natural_t(kIOAsyncCompletionNotificationType),
              contentBytes >= MemoryLayout<kern_return_t>.size else {
            return nil
        }

        let argumentBytes = contentBytes - MemoryLayout<kern_return_t>.size
        guard argumentBytes.isMultiple(of: MemoryLayout<UInt64>.size) else {
            return nil
        }
        let argumentCount = argumentBytes / MemoryLayout<UInt64>.size
        guard argumentCount <= maximumArgumentCount,
              resultOffset + contentBytes <= messageBytes else {
            return nil
        }

        let reference = base.loadUnaligned(
            fromByteOffset: referenceArrayOffset
                + callerReferenceIndex * MemoryLayout<UInt64>.size,
            as: UInt64.self
        )
        let status = base.loadUnaligned(
            fromByteOffset: resultOffset,
            as: kern_return_t.self
        )
        let arguments = (0..<argumentCount).map { index in
            base.loadUnaligned(
                fromByteOffset: resultOffset
                    + MemoryLayout<kern_return_t>.size
                    + index * MemoryLayout<UInt64>.size,
                as: UInt64.self
            )
        }
        return Completion(reference: reference, status: status, arguments: arguments)
    }
}
