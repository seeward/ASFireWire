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
    private var nextCommandId: UInt16 = 1

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

    /// Why the last conversation gave up. A single "the command failed" status
    /// covering write-failed, no-reply and reply-mismatch costs a diagnosis cycle
    /// each time it is hit, so the cause is recorded where callers can surface it.
    private var lastFailure: String?

    func noteFailure(_ reason: String) { lastFailure = reason }

    func takeFailure() -> String? {
        defer { lastFailure = nil }
        return lastFailure
    }

    /// FFADO drives the mailbox from a process-global counter
    /// (`bebob_dl_codes.cpp:27`) and validates the echo on every reply, because
    /// the commandId is the only thing separating two responses to the same
    /// command code (`bebob_dl_mgr.cpp:638-646`). We used to send a constant id
    /// per opcode, which cannot tell a fresh reply from the one the previous
    /// command left sitting in the register.
    func takeCommandId() -> UInt16 {
        let id = nextCommandId
        nextCommandId = nextCommandId == UInt16.max ? 1 : nextCommandId + 1
        return id
    }
}

/// A reply read back from `AddrRegResp`.
///
/// `operandSize` counts operand **quadlets** (FFADO `bebob_dl_codes.cpp:52-64`;
/// `getRespSizeInQuadlets() = 2 + operandSize`). Quadlets past that count are not
/// part of this reply — the device leaves the previous command's operands in
/// place. Measured on a PHASE 88: the `0x09` reply reports `operandSize 0` while
/// the operand word still held the `128` from the preceding `0x08` poll. Reading
/// it as a byte count yields fiction, so `operand(_:)` refuses to hand it out.
struct BeBoBMailboxReply {
    let protocolVersion: UInt32
    let commandId: UInt16
    let opcode: UInt8
    let operandSize: UInt8
    private let operands: [UInt32]

    /// Quadlets the device says this reply occupies, header included.
    var declaredQuadlets: Int { 2 + Int(operandSize) }

    init?(_ raw: Data) {
        guard raw.count >= 8 else { return nil }
        let bytes = [UInt8](raw)
        func quadlet(_ offset: Int) -> UInt32 {
            UInt32(bytes[offset]) |
            (UInt32(bytes[offset + 1]) << 8) |
            (UInt32(bytes[offset + 2]) << 16) |
            (UInt32(bytes[offset + 3]) << 24)
        }
        protocolVersion = quadlet(0)
        commandId = UInt16(bytes[4]) | (UInt16(bytes[5]) << 8)
        opcode = bytes[6]
        operandSize = bytes[7]

        var parsed: [UInt32] = []
        var offset = 8
        while offset + 4 <= bytes.count {
            parsed.append(quadlet(offset))
            offset += 4
        }
        operands = parsed
    }

    /// Nil when the device did not actually supply this operand.
    func operand(_ index: Int) -> UInt32? {
        guard index >= 0, index < Int(operandSize), index < operands.count else { return nil }
        return operands[index]
    }

    func answers(protocolVersion expectedVersion: UInt32, commandId expectedId: UInt16, opcode expectedOpcode: UInt8) -> Bool {
        protocolVersion == expectedVersion && commandId == expectedId && opcode == expectedOpcode
    }
}

extension ASFWDriverConnector {
    private static let beBoBAddrHi: UInt16 = 0xFFFF
    private static let beBoBReqAddrLo: UInt32 = 0xC802_1000
    private static let beBoBReqBufAddrLo: UInt32 = 0xC802_1040
    private static let beBoBRespAddrLo: UInt32 = 0xC802_9000
    private static let beBoBRespBufAddrLo: UInt32 = 0xC802_9040

    private static let beBoBProtocolVersion: UInt32 = 1

    /// Header plus three operands. Covers every reply shape observed on hardware
    /// (`0x08` reports operandSize 3; `0x07` and `0x09` report 0), so the common
    /// case costs one read. A reply declaring more is re-read at its full size.
    private static let respPrefetchQuadlets = 5

    /// A reply does not necessarily land in the register by the time the write
    /// ack returns, so a mismatched echo is retried rather than believed. The wait
    /// escalates: the reply is normally there on the first read, but the device
    /// answers more slowly as its echo backlog grows, and a flat few-millisecond
    /// budget gave up part way through a line.
    private static let respBackoffNs: [UInt64] = [
        500_000, 500_000, 1_000_000, 1_000_000, 2_000_000, 2_000_000,
        5_000_000, 5_000_000, 10_000_000, 10_000_000, 20_000_000, 20_000_000,
    ]

    private static let quietPollGapNs: UInt64 = 40_000_000  // 40ms
    private static let commandSettleNs: UInt64 = 200_000_000 // 200ms

    // MARK: - Block Transaction Helpers

    /// Block transactions against this mailbox must be a whole number of
    /// quadlets. An unaligned request-buffer write drops its tail: `"help\r\n"`
    /// lands as `"help"` and the CR/LF is replaced by whatever the previous
    /// command left at those offsets, so the device echoes a garbled line and
    /// never executes it. Padding here is safe because the request envelope
    /// carries the true byte count separately.
    private static func quadletAligned(_ length: Int) -> Int {
        (length + 3) & ~3
    }

    /// The transaction result is usually ready on the first look; the early
    /// iterations poll tightly so a per-character shell write is not paced by a
    /// fixed millisecond floor.
    private func awaitTransaction<T>(_ attempt: () -> T?) async -> T? {
        for index in 0..<40 {
            if let result = attempt() {
                return result
            }
            try? await Task.sleep(nanoseconds: index < 8 ? 500_000 : 5_000_000)
        }
        return nil
    }

    private func performBlockWrite(deviceID: DeviceInstanceID, addressLow: UInt32, payload: Data) async -> Bool {
        var padded = payload
        padded.append(contentsOf: repeatElement(UInt8(0), count: Self.quadletAligned(payload.count) - payload.count))
        guard let handle = asyncBlockWrite(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, payload: padded) else {
            return false
        }
        let status = await awaitTransaction { getTransactionResult(handle: handle)?.status }
        return status == 0
    }

    private func performBlockRead(deviceID: DeviceInstanceID, addressLow: UInt32, length: UInt32) async -> Data? {
        // The stdout FIFO's tail page is almost never a multiple of four, so
        // read the aligned length and trim. Asking for the raw length instead
        // fails the transaction outright and loses the end of every response.
        let aligned = UInt32(Self.quadletAligned(Int(length)))
        guard let handle = asyncBlockRead(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, length: aligned) else {
            return nil
        }
        let result = await awaitTransaction { getTransactionResult(handle: handle, initialPayloadCapacity: Int(aligned)) }
        guard let result, result.status == 0 else { return nil }
        return result.payload.prefix(Int(length))
    }

    // MARK: - 12-byte Envelope Encoding (Little Endian)

    /// `operandSize` is a count of operand **quadlets**, not bytes — FFADO
    /// `bebob_dl_codes.cpp:52-64`. The driver's bootloader-window guard accepts
    /// exactly 12 bytes here, so the envelope always carries one operand slot
    /// even when the command declares none.
    private func makeEnvelope(commandId: UInt16, opcode: UInt8, operandSize: UInt8, operand: UInt32) -> Data {
        var data = Data(count: 12)
        let version = Self.beBoBProtocolVersion
        data[0] = UInt8(version & 0xFF)
        data[1] = UInt8((version >> 8) & 0xFF)
        data[2] = UInt8((version >> 16) & 0xFF)
        data[3] = UInt8((version >> 24) & 0xFF)

        data[4] = UInt8(commandId & 0xFF)
        data[5] = UInt8((commandId >> 8) & 0xFF)
        data[6] = opcode
        data[7] = operandSize

        data[8] = UInt8(operand & 0xFF)
        data[9] = UInt8((operand >> 8) & 0xFF)
        data[10] = UInt8((operand >> 16) & 0xFF)
        data[11] = UInt8((operand >> 24) & 0xFF)
        return data
    }

    private func readMailboxReply(deviceID: DeviceInstanceID) async -> BeBoBMailboxReply? {
        guard let head = await performBlockRead(deviceID: deviceID,
                                                addressLow: Self.beBoBRespAddrLo,
                                                length: UInt32(Self.respPrefetchQuadlets * 4)),
              let reply = BeBoBMailboxReply(head) else {
            return nil
        }
        guard reply.declaredQuadlets > Self.respPrefetchQuadlets else {
            return reply
        }
        guard let full = await performBlockRead(deviceID: deviceID,
                                                addressLow: Self.beBoBRespAddrLo,
                                                length: UInt32(reply.declaredQuadlets * 4)) else {
            return reply
        }
        return BeBoBMailboxReply(full) ?? reply
    }

    /// Issues one mailbox command and returns the device's own answer to it.
    ///
    /// The 1394 write ack only says the mailbox accepted the bytes; what the
    /// device *did* with the command is readable only from `AddrRegResp`
    /// (`docs/BEBOB_BRIDGECO_REFERENCE.md:148`). Inferring success from the ack
    /// is how a device that silently accepted one byte of a ten-byte command
    /// went undiagnosed.
    private func mailboxCommand(deviceID: DeviceInstanceID,
                                opcode: UInt8,
                                operandSize: UInt8,
                                operand: UInt32) async -> BeBoBMailboxReply? {
        let commandId = await BeBoBMailboxGate.shared.takeCommandId()
        let envelope = makeEnvelope(commandId: commandId, opcode: opcode, operandSize: operandSize, operand: operand)
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: envelope) else {
            await BeBoBMailboxGate.shared.noteFailure(
                String(format: "request write failed for opcode 0x%02x (commandId %u)", opcode, commandId))
            return nil
        }

        var lastSeen = "no reply read"
        for delay in Self.respBackoffNs {
            if let reply = await readMailboxReply(deviceID: deviceID) {
                if reply.answers(protocolVersion: Self.beBoBProtocolVersion, commandId: commandId, opcode: opcode) {
                    return reply
                }
                lastSeen = String(format: "version %u, commandId %u, opcode 0x%02x",
                                  reply.protocolVersion, reply.commandId, reply.opcode)
            }
            try? await Task.sleep(nanoseconds: delay)
        }
        await BeBoBMailboxGate.shared.noteFailure(
            String(format: "no reply matching opcode 0x%02x commandId %u; last saw %@",
                   opcode, commandId, lastSeen))
        return nil
    }

    // MARK: - Virtual UART

    /// 0x07 SwitchTo1394Shell — re-points the device console's stdio at the 1394
    /// mailbox.
    ///
    /// Not required on every BeBoB device, and deliberately not on the connect
    /// path: a freshly booted PHASE 88 already prints `CMDLINE tool ready for
    /// commands` and its `/cfg>` prompt into the mailbox with no switch at all.
    /// Kept because it is a real protocol operation, verified on hardware — the
    /// device answers it by printing `1394 request instigating switch to 1394
    /// shell` — and the next BeBoB device may well need it.
    public func enableBeBoB1394Shell(deviceID: DeviceInstanceID) async -> Bool {
        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        return await mailboxCommand(deviceID: deviceID, opcode: 0x07, operandSize: 0, operand: 0) != nil
    }

    /// Feeds the shell one character per WriteShellChars command.
    ///
    /// Measured on a TerraTec PHASE 88 Rack FW: the device delivers exactly ONE
    /// byte per `0x09` whatever the operand byte count says, so `"help\r\n"` sent
    /// as a single command reaches the shell as `h` — the CR never arrives and
    /// the line is echoed a character at a time but never runs. An operand above
    /// 1 is worse than useless: it latches that buffer, and every later `0x09`
    /// serves `latched[i % operand]` while the device stops re-reading the
    /// request buffer entirely, ACKing writes it ignores, until it is power
    /// cycled.
    ///
    /// `operand == 1` is the only width known safe on every BeBoB device — the
    /// 1814 honours any width, so one byte per command is a subset of what it
    /// already does — which is why there is no per-device branch here. It also
    /// makes the stale-tail bug structurally impossible: a one-byte payload has
    /// no tail for the previous command's bytes to occupy.
    private func writeShellChars(deviceID: DeviceInstanceID, text: String) async -> Bool {
        let bytes = Array(text.utf8)
        for (index, byte) in bytes.enumerated() {
            guard await performBlockWrite(deviceID: deviceID,
                                          addressLow: Self.beBoBReqBufAddrLo,
                                          payload: Data([byte, 0, 0, 0])) else {
                await BeBoBMailboxGate.shared.noteFailure(
                    "request buffer write failed at byte \(index + 1) of \(bytes.count)")
                return false
            }
            // The 0x09 reply carries no operands (operandSize 0), so it cannot
            // report an accepted byte count. Matching its echo still proves the
            // device processed *this* command rather than re-serving an old one.
            guard await mailboxCommand(deviceID: deviceID, opcode: 0x09, operandSize: 1, operand: 1) != nil else {
                let reason = await BeBoBMailboxGate.shared.takeFailure() ?? "unknown"
                await BeBoBMailboxGate.shared.noteFailure(
                    "stopped after \(index) of \(bytes.count) characters: \(reason)")
                return false
            }
        }
        return true
    }

    /// The BridgeCo shell prints its prompt when it is ready for the next
    /// command — `/cfg>` on the PHASE 88, `1814>` on the M-Audio. Seeing one is
    /// positive evidence the command finished, which a run of quiet polls only
    /// ever guesses at. The command echo cannot be mistaken for it: every echo
    /// redraw ends with a space and a backspace, never with `>`.
    private static func endsAtShellPrompt(_ data: Data) -> Bool {
        var index = data.endIndex
        while index > data.startIndex {
            let previous = data.index(before: index)
            let byte = data[previous]
            if byte == 0x20 || byte == 0x0D || byte == 0x0A {
                index = previous
                continue
            }
            return byte == 0x3E // '>'
        }
        return false
    }

    /// Drains available stdout characters from the DM1000 FIFO. Serialised
    /// against every other mailbox conversation.
    public func drainStdoutFIFO(deviceID: DeviceInstanceID, maxChunks: Int = 32) async -> String {
        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        return await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: maxChunks, stopAtPrompt: false)
    }

    /// Caller must already hold `BeBoBMailboxGate`.
    private func drainStdoutFIFOLocked(deviceID: DeviceInstanceID,
                                       maxChunks: Int,
                                       quietRoundsBeforeStop: Int = 5,
                                       stopAtPrompt: Bool = true) async -> String {
        var result = Data()
        var quietRounds = 0

        for _ in 0..<maxChunks {
            guard let reply = await mailboxCommand(deviceID: deviceID, opcode: 0x08, operandSize: 1, operand: 1024),
                  let availableBytes = reply.operand(0) else {
                break
            }

            // stdout arrives in bursts as the RTOS produces it, and the FIFO lags
            // the shell by hundreds of bytes, so a single empty poll does not mean
            // the response is complete. Only a run of them does — and the prompt
            // check above ends the common case long before that run is needed.
            if availableBytes == 0 {
                quietRounds += 1
                if quietRounds >= quietRoundsBeforeStop {
                    break
                }
                try? await Task.sleep(nanoseconds: Self.quietPollGapNs)
                continue
            }
            quietRounds = 0

            let bytesToRead = min(availableBytes, 1024)
            guard let stdoutData = await performBlockRead(deviceID: deviceID,
                                                          addressLow: Self.beBoBRespBufAddrLo,
                                                          length: bytesToRead) else {
                break
            }
            result.append(stdoutData)

            if stopAtPrompt && Self.endsAtShellPrompt(result) {
                break
            }
        }

        // The shell emits ASCII plus backspace; decoding leniently keeps a page
        // with one stray byte from discarding the whole response, which the old
        // utf8-then-ascii pair did silently.
        return String(decoding: result, as: UTF8.self)
    }

    public func executeBeBoBShellCommand(deviceID: DeviceInstanceID, command: String) async -> String? {
        // The shell terminates lines on CRLF. A bare LF is echoed but never
        // executed, so normalise whatever the caller passed.
        var cmd = command
        while cmd.hasSuffix("\n") || cmd.hasSuffix("\r") {
            cmd.removeLast()
        }
        cmd += "\r\n"

        await BeBoBMailboxGate.shared.acquire()
        defer { Task { await BeBoBMailboxGate.shared.release() } }

        // Whatever a previous conversation left half-typed is still sitting in
        // the device's line editor, and the shell has no way to tell us. An
        // aborted write leaves e.g. "h" there, so the next command arrives as
        // "hhelp" and is rejected as an unknown command. A bare CRLF commits and
        // clears that line — at worst printing one "Unknown command" — and the
        // drain below throws the result away.
        _ = await writeShellChars(deviceID: deviceID, text: "\r\n")

        // Discard anything already queued so the drain below returns this
        // command's output rather than the tail of the previous one. Draining to
        // quiet rather than to a prompt, because stale backlog usually ends at a
        // prompt and would stop this early.
        _ = await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: 24,
                                        quietRoundsBeforeStop: 2, stopAtPrompt: false)

        guard await writeShellChars(deviceID: deviceID, text: cmd) else {
            return nil
        }

        // Yield to let the KnOS shell task execute the command
        try? await Task.sleep(nanoseconds: Self.commandSettleNs)

        return await drainStdoutFIFOLocked(deviceID: deviceID, maxChunks: 64)
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
