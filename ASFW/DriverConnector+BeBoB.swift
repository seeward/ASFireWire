// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// DriverConnector+BeBoB.swift — Virtual UART and Telemetry driver bridge for BeBoB devices.

import Foundation

public struct BeBoBSwiftStreamingStats: Equatable, Sendable {
    public var rxPackets: UInt64 = 0
    public var onlyHeaders: UInt64 = 0
    public var rxEmptyPkt: UInt64 = 0
    public var rxNoMem: UInt64 = 0
    public var rxToLong: UInt64 = 0
    public var rxPktToLong: UInt64 = 0
    public var rxPktToSmall: UInt64 = 0
    public var rxDmaBusy: UInt64 = 0
    public var rxQFull: UInt64 = 0
    public var rxQFillLevelPct: UInt32 = 0
    public var poolFillLevelPct: UInt32 = 0

    public var ctrDiffErr: UInt64 = 0
    public var sytDiffErr: UInt64 = 0
    public var sumDiffErr: UInt64 = 0
    public var bcoHdrErr: UInt64 = 0

    public var pktFuture: UInt64 = 0
    public var pktPast: UInt64 = 0
    public var pktSytDiff: Int32 = 0
    public var sytOffset: UInt32 = 0
    public var sytCorr: Int32 = 0

    public var rxIsr: UInt64 = 0
    public var txIsr: UInt64 = 0
}

public struct BeBoBSwiftAvStat: Equatable, Sendable {
    public var setTgInLock: Bool = false
    public var setTgSytMiss: Bool = false
    public var cipMismatch: Bool = false
    public var dbcMismatch: Bool = false
    public var headerMismatch: Bool = false
}

public struct BeBoBSwiftSyncState: Equatable, Sendable {
    public var audioState: String = "Unknown"
    public var syncSource: String = "Unknown"
    public var sampleRateHz: UInt32 = 0
}

extension ASFWDriverConnector {
    private static let beBoBAddrHi: UInt16 = 0xFFFF
    private static let beBoBReqAddrLo: UInt32 = 0xC802_1000
    private static let beBoBReqBufAddrLo: UInt32 = 0xC802_1040
    private static let beBoBRespAddrLo: UInt32 = 0xC802_9000
    private static let beBoBRespBufAddrLo: UInt32 = 0xC802_9040

    // MARK: - Synchronous Block Transaction Helpers with Polling

    private func performBlockWrite(deviceID: DeviceInstanceID, addressLow: UInt32, payload: Data) async -> Bool {
        guard let handle = asyncBlockWrite(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, payload: payload) else {
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
        guard let handle = asyncBlockRead(deviceID: deviceID, addressHigh: Self.beBoBAddrHi, addressLow: addressLow, length: length) else {
            return nil
        }
        for _ in 0..<20 {
            if let result = getTransactionResult(handle: handle, initialPayloadCapacity: Int(length)) {
                if result.status == 0 {
                    return result.payload
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
        let envelope = makeEnvelope(commandId: 1, opcode: 0x07, operandSize: 0, operand: 0)
        return await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: envelope)
    }

    public func executeBeBoBShellCommand(deviceID: DeviceInstanceID, command: String) async -> String? {
        var cmd = command
        if !cmd.hasSuffix("\n") {
            cmd += "\r\n"
        }
        guard let cmdData = cmd.data(using: .utf8) else { return nil }

        // Step 1: Write command payload to Request Buffer (0xFFFF_C802_1040)
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqBufAddrLo, payload: cmdData) else {
            return nil
        }

        // Step 2: Commit WriteShellChars envelope (Opcode 0x09) to AddrRegReq
        let writeEnv = makeEnvelope(commandId: 2, opcode: 0x09, operandSize: 1, operand: UInt32(cmdData.count))
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: writeEnv) else {
            return nil
        }

        // Small yield to let ThreadX RTOS process the command line
        try? await Task.sleep(nanoseconds: 20_000_000) // 20ms

        // Step 3: Request ReadShellChars envelope (Opcode 0x08, maxBytes=1024)
        let readEnv = makeEnvelope(commandId: 3, opcode: 0x08, operandSize: 1, operand: 1024)
        guard await performBlockWrite(deviceID: deviceID, addressLow: Self.beBoBReqAddrLo, payload: readEnv) else {
            return nil
        }

        // Step 4: Read 12-byte response from AddrRegResp (0xFFFF_C802_9000)
        guard let respEnvData = await performBlockRead(deviceID: deviceID, addressLow: Self.beBoBRespAddrLo, length: 12),
              let availableBytes = decodeResponseOperand(respEnvData), availableBytes > 0 else {
            return ""
        }

        let bytesToRead = min(availableBytes, 1024)

        // Step 5: Read stdout payload from AddrRegRespBuf (0xFFFF_C802_9040)
        guard let stdoutData = await performBlockRead(deviceID: deviceID, addressLow: Self.beBoBRespBufAddrLo, length: bytesToRead) else {
            return ""
        }

        return String(data: stdoutData, encoding: .utf8) ?? String(data: stdoutData, encoding: .ascii)
    }

    // MARK: - Telemetry Parsers

    public func fetchBeBoBStreamingStats(deviceID: DeviceInstanceID) async -> BeBoBSwiftStreamingStats? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "sys stat") else {
            return nil
        }
        var stats = BeBoBSwiftStreamingStats()
        let lines = output.components(separatedBy: .newlines)
        for line in lines {
            if line.contains("rxPackets") { stats.rxPackets = extractUInt64(line, "rxPackets") }
            if line.contains("onlyHeaders") { stats.onlyHeaders = extractUInt64(line, "onlyHeaders") }
            if line.contains("rxEmptyPkt") { stats.rxEmptyPkt = extractUInt64(line, "rxEmptyPkt") }
            if line.contains("rxNoMem") { stats.rxNoMem = extractUInt64(line, "rxNoMem") }
            if line.contains("rxToLong") { stats.rxToLong = extractUInt64(line, "rxToLong") }
            if line.contains("rxPktToLong") { stats.rxPktToLong = extractUInt64(line, "rxPktToLong") }
            if line.contains("rxPktToSmall") { stats.rxPktToSmall = extractUInt64(line, "rxPktToSmall") }
            if line.contains("rxDmaBusy") { stats.rxDmaBusy = extractUInt64(line, "rxDmaBusy") }
            if line.contains("rxQFull") { stats.rxQFull = extractUInt64(line, "rxQFull") }
            if line.contains("rxQFillLevel") { stats.rxQFillLevelPct = UInt32(extractUInt64(line, "rxQFillLevel")) }
            if line.contains("PoolFillLevel") { stats.poolFillLevelPct = UInt32(extractUInt64(line, "PoolFillLevel")) }

            if line.contains("CtrDiffErr") { stats.ctrDiffErr = extractUInt64(line, "CtrDiffErr") }
            if line.contains("SytDiffErr") { stats.sytDiffErr = extractUInt64(line, "SytDiffErr") }
            if line.contains("SumDiffErr") { stats.sumDiffErr = extractUInt64(line, "SumDiffErr") }
            if line.contains("BCOHdrErr") { stats.bcoHdrErr = extractUInt64(line, "BCOHdrErr") }

            if line.contains("pkt Future") { stats.pktFuture = extractUInt64(line, "pkt Future") }
            if line.contains("pkt Past") { stats.pktPast = extractUInt64(line, "pkt Past") }
            if line.contains("pktSytDiff") { stats.pktSytDiff = Int32(extractInt64(line, "pktSytDiff")) }
            if line.contains("SytOffset") { stats.sytOffset = UInt32(extractUInt64(line, "SytOffset")) }
            if line.contains("SytCorr") { stats.sytCorr = Int32(extractInt64(line, "SytCorr")) }
        }
        return stats
    }

    public func fetchBeBoBAvStat(deviceID: DeviceInstanceID) async -> BeBoBSwiftAvStat? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "sys avstat all") else {
            return nil
        }
        var stat = BeBoBSwiftAvStat()
        if output.contains("SetTgInLock") || output.contains("TGEN in lock") { stat.setTgInLock = true }
        if output.contains("SetTgSytMiss") || output.contains("SytMiss") { stat.setTgSytMiss = true }
        if output.contains("CIPMismatch") { stat.cipMismatch = true }
        if output.contains("DBCMismatch") { stat.dbcMismatch = true }
        if output.contains("HeaderMismatch") { stat.headerMismatch = true }
        return stat
    }

    public func fetchBeBoBSyncState(deviceID: DeviceInstanceID) async -> BeBoBSwiftSyncState? {
        guard let output = await executeBeBoBShellCommand(deviceID: deviceID, command: "fw sync show") else {
            return nil
        }
        var sync = BeBoBSwiftSyncState()
        if output.contains("Waiting for sync") { sync.audioState = "Waiting for sync" }
        else if output.contains("Running") { sync.audioState = "Running" }
        else if output.contains("Idle") { sync.audioState = "Idle" }
        else if output.contains("Stop") { sync.audioState = "Stop" }

        if output.contains("Internal Digital Input Sync") { sync.syncSource = "Internal Digital Input" }
        else if output.contains("Internal Sync") { sync.syncSource = "Internal" }
        else if output.contains("Adat External Sync") { sync.syncSource = "ADAT External" }
        else if output.contains("Spdif External Sync") { sync.syncSource = "S/PDIF External" }
        else if output.contains("Word Clock Sync") { sync.syncSource = "Word Clock" }

        if output.contains("48kHz") { sync.sampleRateHz = 48000 }
        else if output.contains("44.1kHz") { sync.sampleRateHz = 44100 }
        else if output.contains("96kHz") { sync.sampleRateHz = 96000 }
        else if output.contains("88.2kHz") { sync.sampleRateHz = 88200 }
        else if output.contains("192kHz") { sync.sampleRateHz = 192000 }
        return sync
    }

    private func extractUInt64(_ text: String, _ key: String) -> UInt64 {
        guard let range = text.range(of: key) else { return 0 }
        let sub = text[range.upperBound...]
        let digits = sub.trimmingCharacters(in: CharacterSet.whitespaces.union(CharacterSet(charactersIn: ":=")))
        let scanner = Scanner(string: digits)
        return scanner.scanUInt64() ?? 0
    }

    private func extractInt64(_ text: String, _ key: String) -> Int64 {
        guard let range = text.range(of: key) else { return 0 }
        let sub = text[range.upperBound...]
        let digits = sub.trimmingCharacters(in: CharacterSet.whitespaces.union(CharacterSet(charactersIn: ":=")))
        let scanner = Scanner(string: digits)
        return scanner.scanInt64() ?? 0
    }
}
