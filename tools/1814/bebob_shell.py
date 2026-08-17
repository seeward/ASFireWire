#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 ASFireWire Project
#
# bebob_shell.py — drive the BridgeCo BeBoB Virtual UART shell over the ASFW
# MCP control plane, using only raw async block transactions.
#
# This is the reference driver for the mailbox protocol: the Swift client in
# ASFW/DriverConnector+BeBoB.swift implements the same sequence, and the wire
# facts below were measured against an M-Audio FireWire 1814 on 2026-08-17.
#
#   * Block transactions must be a whole number of quadlets. An unaligned
#     request-buffer write silently drops its tail, so "help\r\n" lands as
#     "help" with the CR/LF replaced by whatever the previous command left at
#     those offsets — the device then echoes a garbled line and never executes
#     it. Pad the payload; keep the envelope's operand at the true length.
#   * stdout is paged at 128 bytes. The 0x9000 envelope operand is the count.
#   * Output arrives in bursts, so a short page does NOT mean "drained". Stop
#     on a run of empty polls instead.
#   * The mailbox is half-duplex and single-occupancy: exactly one conversation
#     at a time, or the device answers rCode 4 (resp_conflict_error).
#   * The shell terminates lines on CRLF. A bare LF is echoed but never runs.
#
# Only the Virtual UART opcodes (0x07/0x08/0x09) are ever emitted; the flash
# and EEPROM opcodes are rejected by assertion.
#
# Usage:
#   ./bebob_shell.py --device 3 --node 2 --gen 6 "sys stat" "fw show"
#   ./bebob_shell.py --drain          # flush the FIFO backlog only

import argparse
import json
import sys
import time
import urllib.request

DEFAULT_ENDPOINT = "http://127.0.0.1:8766/mcp"

ADDR_HI = 0xFFFF
REQ = 0xC8021000          # AddrRegReq     — request envelope (12 bytes)
REQBUF = 0xC8021040       # AddrRegReqBuf  — stdin text
RESP = 0xC8029000         # AddrRegResp    — response envelope (12 bytes)
RESPBUF = 0xC8029040      # AddrRegRespBuf — stdout text

OPCODE_SWITCH_TO_SHELL = 0x07
OPCODE_READ_CHARS = 0x08
OPCODE_WRITE_CHARS = 0x09
PERMITTED_OPCODES = frozenset({0x07, 0x08, 0x09})

PAGE_BYTES = 128
MAX_READ = 1024


class Mailbox:
    def __init__(self, endpoint, device, node, generation):
        self.endpoint = endpoint
        self.device = device
        self.node = node
        self.generation = generation
        self.session = self._session_id()

    def _post(self, payload, headers=None):
        head = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if headers:
            head.update(headers)
        request = urllib.request.Request(
            self.endpoint, data=json.dumps(payload).encode(), headers=head, method="POST"
        )
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                return dict(response.headers), response.read().decode()
        except urllib.error.HTTPError as error:
            return dict(error.headers), error.read().decode() if error.fp else ""

    def _session_id(self):
        # The app serves a single global MCP session. Initialising when one
        # already exists still returns its id in the response header.
        headers, _ = self._post({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                       "clientInfo": {"name": "bebob_shell", "version": "1"}},
        })
        session = next((v for k, v in headers.items() if k.lower() == "mcp-session-id"), None)
        if not session:
            sys.exit(f"no MCP session from {self.endpoint}; is ASFW.app running?")
        return session

    def _call(self, tool, arguments):
        _, body = self._post(
            {"jsonrpc": "2.0", "id": 99, "method": "tools/call",
             "params": {"name": tool, "arguments": arguments}},
            {"Mcp-Session-Id": self.session},
        )
        for line in body.splitlines():
            if line.startswith("data: ") and line[6:].lstrip().startswith("{"):
                message = json.loads(line[6:])
                for item in message.get("result", {}).get("content", []):
                    if item.get("type") == "text":
                        return json.loads(item["text"])
        return {"ok": False}

    def _address(self, address_low, extra):
        arguments = {"deviceInstanceId": self.device, "nodeId": self.node,
                     "generation": self.generation, "addressHigh": ADDR_HI,
                     "addressLow": address_low}
        arguments.update(extra)
        return arguments

    def read(self, address_low, length):
        """Block reads must be a non-zero multiple of 4; trim the padding back."""
        aligned = (length + 3) & ~3
        result = self._call("asfw_read_block", self._address(address_low, {"length": aligned}))
        data = result.get("data", {})
        if not data.get("ok"):
            return None
        return bytes(data["payload"])[:length]

    def write(self, address_low, payload):
        padded = list(payload) + [0] * (-len(payload) % 4)
        result = self._call("asfw_write_block", self._address(address_low, {"payload": padded}))
        return bool(result.get("data", {}).get("ok"))

    @staticmethod
    def envelope(command_id, opcode, operand_size, operand):
        assert opcode in PERMITTED_OPCODES, f"opcode 0x{opcode:02x} is not a Virtual UART opcode"
        return bytes([
            1, 0, 0, 0,
            command_id & 0xFF, (command_id >> 8) & 0xFF, opcode, operand_size,
            operand & 0xFF, (operand >> 8) & 0xFF,
            (operand >> 16) & 0xFF, (operand >> 24) & 0xFF,
        ])

    def drain(self, max_chunks=40, settle=0.015, quiet_rounds=3):
        out = bytearray()
        quiet = 0
        for _ in range(max_chunks):
            if not self.write(REQ, self.envelope(3, OPCODE_READ_CHARS, 1, MAX_READ)):
                break
            envelope = self.read(RESP, 12)
            if envelope is None:
                break
            available = int.from_bytes(envelope[8:12], "little")
            if available == 0:
                quiet += 1
                if quiet >= quiet_rounds:
                    break
                time.sleep(settle)
                continue
            quiet = 0
            chunk = self.read(RESPBUF, min(available, MAX_READ))
            if chunk is None:
                break
            out += chunk
            time.sleep(settle)
        return bytes(out)

    def run(self, command, settle=0.2):
        self.drain(max_chunks=24, quiet_rounds=2)          # discard stale output
        line = (command.rstrip("\r\n") + "\r\n").encode("ascii")
        self.write(REQBUF, line)
        self.write(REQ, self.envelope(2, OPCODE_WRITE_CHARS, 1, len(line)))
        time.sleep(settle)
        return self.drain().decode("ascii", "replace")


def main():
    parser = argparse.ArgumentParser(
        description="Drive the BridgeCo BeBoB Virtual UART shell via the ASFW MCP control plane.")
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument("--device", type=int, default=3, help="deviceInstanceId")
    parser.add_argument("--node", type=int, default=2)
    parser.add_argument("--gen", type=int, default=6, help="bus generation")
    parser.add_argument("--drain", action="store_true", help="flush the FIFO and exit")
    parser.add_argument("commands", nargs="*")
    args = parser.parse_args()

    mailbox = Mailbox(args.endpoint, args.device, args.node, args.gen)

    if args.drain:
        print(mailbox.drain(max_chunks=80).decode("ascii", "replace"))
        return

    for command in args.commands:
        print(f"\n{'=' * 70}\n$ {command}\n{'=' * 70}")
        print(mailbox.run(command))


if __name__ == "__main__":
    main()
