// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DriverConnector+BeBoB.swift — Virtual UART and Telemetry driver bridge for BeBoB devices.

import Foundation

/// The DM1000 mailbox is half-duplex and single-occupancy: overlapping 1394
/// requests are answered with rCode 4 (resp_conflict_error), and a drain that
/// loses its request/response pairing silently returns another command's
/// output. Every Virtual UART conversation holds this gate for the whole
/// exchange, not just for a single transaction.
actor BeBoBMailboxGate {
    static let shared = BeBoBMailboxGate()

    private var busy = false
    private var waiters: [CheckedContinuation<Void, Never>] = []

    func acquire() async {
        while busy {
            await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
                waiters.append(continuation)
            }
        }
        busy = true
    }

    func release() {
        busy = false
        if !waiters.isEmpty {
            waiters.removeFirst().resume()
        }
    }
}

extension ASFWDriverConnector {
    private static let beBoBAddrHi: UInt16 = 0xFFFF
    private static let beBoBReqAddrLo: UInt32 = 0xC802_1000
    private static let beBoBReqBufAddrLo: UInt32 = 0xC802_1040
    private static let beBoBRespAddrLo: UInt32 = 0xC802_9000
    private static let beBoBRespBufAddrLo: UInt32 = 0xC802_9040

    // MARK: - Synchronous Block Transaction Helpers with Polling

    /// Block transactions against this mailbox must be a whole number of
    /// quadlets. An unaligned request-buffer write drops its tail: `"help\r\n"`
    /// lands as `"help"` and the CR/LF is replaced by whatever the previous
    /// command left at those offsets, so the device echoes a garbled line and
    /// never executes it. Padding here is safe because the request envelope
    /// carries the true byte count separately.
    private static func quadletAligned(_ length: Int) -> Int {
        (length + 3) & ~3
    }

    private func performBlockWrite(deviceID: DeviceInstanceID, addressLow: UInt32, payload: Data) async -> Bool {
        var padded = payload
        padded.append(contentsOf: repeatElement(UInt8(0), count: Self.quadletAligned(payload.count) - payload.count))
        guard let handle = asyncBlockWrite(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, payload: padded) else {
            return false
        }
        for _ in 0..<20 {
            if let result = getTransactionResult(handle: handle) {
                return result.status == 0
            }
            try? await Task.sleep(nanoseconds: 5_000_000) // 5ms
        }
        return false
    }

    private func performBlockRead(deviceID: DeviceInstanceID, addressLow: UInt32, length: UInt32) async -> Data? {
        // The stdout FIFO's tail page is almost never a multiple of four, so
        // read the aligned length and trim. Asking for the raw length instead
        // fails the transaction outright and loses the end of every response.
        let aligned = UInt32(Self.quadletAligned(Int(length)))
        guard let handle = asyncBlockRead(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, length: aligned) else {
            return nil
        }
        for _ in 0..<20 {
            if let result = getTransactionResult(handle: handle, initialPayloadCapacity: Int(aligned)) {
                if result.status == 0 {
                    return result.payload.prefix(Int(length))
                }
                return nil
            }
            try? await Task.sleep(nanoseconds: 5_000_000) // 5ms
        }
        return nil
    }

    // MARK: - 12-byte Envelope Encoding / Decoding (Little Endian)

    private func makeEnvelope(protocolVersion: UInt32 = 1, commandId: UInt16, opcode: UInt8, operandSize: UInt8, operand: UInt32) -> Data {
        var data = Data(count: 12)
        // Quadlet 0: protocolVersion
        data[0] = UInt8(protocolVersion & 0xFF)
        data[1] = UInt8((protocolVersion >> 8) & 0xFF)
        data[2] = UInt8((protocolVersion >> 16) & 0xFF)
        data[3] = UInt8((protocolVersion >> 24) & 0xFF)

        // Quadlet 1: (operandSize << 24) | (opcode << 16) | commandId
        data[4] = UInt8(commandId & 0xFF)
        data[5] = UInt8((commandId >> 8) & 0xFF)
        data[6] = opcode
        data[7] = operandSize

        // Quadlet 2: operand
        data[8] = UInt8(operand & 0xFF)
        data[9] = UInt8((operand >> 8) & 0xFF)
        data[10] = UInt8((operand >> 16) & 0xFF)
        data[11] = UInt8((operand >> 24) & 0xFF)
        return data
    }

    private func decodeResponseOperand(_ data: Data) -> UInt32? {
        guard data.count >= 12 else { return nil }
        return UInt32(data[8]) |
               (UInt32(data[9]) << 8) |
               (UInt32(data[10]) << 16) |
               (UInt32(data[11]) << 24)
    }

    // MARK: - Virtual UART High-Level Execution

    public func enableBeBoB1394Shell(deviceID: DeviceInstanceID) async -> Bool {
        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        let envelope = makeEnvelope(commandId: 1, opcode: 0x07, operandSize: 0, operand: 0)
        return await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: envelope)
    }

    /// Drains available stdout characters from the DM1000 FIFO until it reports
    /// empty. Serialised against every other mailbox conversation.
    public func drainStdoutFIFO(deviceID: DeviceInstanceID, maxChunks: Int = 32) async -> String {
        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        return await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: maxChunks)
    }

    /// Caller must already hold `BeBoBMailboxGate`.
    private func drainStdoutFIFOLocked(deviceID: DeviceInstanceID,
                                       maxChunks: Int,
                                       quietRoundsBeforeStop: Int = 3) async -> String {
        var result = ""
        var quietRounds = 0

        for _ in 0..<maxChunks {
            let readEnv = makeEnvelope(commandId: 3, opcode: 0x08, operandSize: 1, operand: 1024)
            guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: readEnv) else {
                break
            }

            guard let respEnvData = await performBlockRead(deviceID: deviceID, addressLow: Self.beBoBRespAddrLo, length: 12),
                  let availableBytes = decodeResponseOperand(respEnvData) else {
                break
            }

            // stdout arrives in bursts as the RTOS produces it, so a single
            // empty poll does not mean the response is complete. Only a run of
            // them does. The old terminator — "fewer than 128 bytes available"
            // — stopped on the first short page, which is normally the *first*
            // page of a reply rather than the last.
            if availableBytes == 0 {
                quietRounds += 1
                if quietRounds >= quietRoundsBeforeStop {
                    break
                }
                try? await Task.sleep(nanoseconds: 15_000_000) // 15ms
                continue
            }
            quietRounds = 0

            let bytesToRead = min(availableBytes, 1024)
            guard let stdoutData = await performBlockRead(deviceID: deviceID, addressLow: Self.beBoBRespBufAddrLo, length: bytesToRead) else {
                break
            }

            if let str = String(data: stdoutData, encoding: .utf8) ?? String(data: stdoutData, encoding: .ascii), !str.isEmpty {
                result += str
            }

            try? await Task.sleep(nanoseconds: 15_000_000) // 15ms inter-chunk yield
        }
        return result
    }

    public func executeBeBoBShellCommand(deviceID: DeviceInstanceID, command: String) async -> String? {
        // The shell terminates lines on CRLF. A bare LF is echoed but never
        // executed, so normalise whatever the caller passed.
        var cmd = command
        while cmd.hasSuffix("\n") || cmd.hasSuffix("\r") {
            cmd.removeLast()
        }
        cmd += "\r\n"
        guard let cmdData = cmd.data(using: .utf8) else { return nil }

        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        // Discard anything already queued so the drain below returns this
        // command's output rather than the tail of the previous one.
        _ = await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: 24, quietRoundsBeforeStop: 2)

        // Step 1: Write command payload to Request Buffer (0xFFFF_C802_1040).
        // performBlockWrite pads to a quadlet; the envelope below carries the
        // true, unpadded length so the device consumes exactly the command.
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqBufAddrLo, payload: cmdData) else {
            return nil
        }

        // Step 2: Commit WriteShellChars envelope (Opcode 0x09) to AddrRegReq
        let writeEnv = makeEnvelope(commandId: 2, opcode: 0x09, operandSize: 1, operand: UInt32(cmdData.count))
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: writeEnv) else {
            return nil
        }

        // Yield to let the KnOS shell task execute the command
        try? await Task.sleep(nanoseconds: 200_000_000) // 200ms

        // Step 3: Drain all output chunks from the FIFO until empty
        let stdout = await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: 40)
        return stdout.isEmpty ? "" : stdout
    }

    // MARK: - Telemetry Parsers

    /// `sys stat` prints one COLUMN PER ISOCHRONOUS STREAM, so a parser that
    /// takes the first value on each row reads whichever stream the firmware
    /// happens to print first. On the 1814 that is iso channel 58 — the
    /// device's internal S/PDIF-ADAT output — not the FireWire stream. Callers
    /// wanting "our" numbers must go through `fireWireOutput` / `fireWireInput`,
    /// which select on the `dest`/`source` row.
    public func fetchBeBoBStreamingStats(deviceID: DeviceInstanceID) async -> BeBoBStreamingStats? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "sys stat") else {
            return nil
        }
        return BeBoBShellTelemetryParser.parseStreamingStats(output)
    }

    /// `sys avstat all` prints every latch unconditionally as
    /// "    SetTgInLock       : 00000001", so the presence of a label says
    /// nothing — only its value does, and the values are hex.
    public func fetchBeBoBAvStat(deviceID: DeviceInstanceID) async -> BeBoBAvStat? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "sys avstat all") else {
            return nil
        }
        return BeBoBShellTelemetryParser.parseAvStat(output)
    }

    /// `fw show` carries audio state, sync source, sample rate, digital format
    /// and the iso channel assignments together. `fw sync show` is a valid
    /// command but reports only the sync source.
    public func fetchBeBoBSyncState(deviceID: DeviceInstanceID) async -> BeBoBSyncState? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "fw show") else {
            return nil
        }
        return BeBoBShellTelemetryParser.parseSyncState(output)
    }
}
