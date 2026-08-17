#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 ASFireWire Project
#
# bebob_diag.py — Standalone BeBoB Virtual UART & Streaming Diagnostics Tool.
#
# Connects to a BridgeCo BeBoB device (e.g. M-Audio FW 1814) via ASFW MCP control plane
# or raw async FireWire transactions, allowing interactive shell access and telemetry dumping.

import argparse
import json
import sys
import urllib.request
import urllib.error

DEFAULT_ENDPOINT = "http://127.0.0.1:8766/mcp"

def call_mcp_tool(endpoint: str, tool_name: str, arguments: dict) -> dict:
    url = endpoint.rstrip("/")
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": tool_name,
            "arguments": arguments
        }
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data.get("result", {})
    except urllib.error.URLError as e:
        print(f"Error connecting to ASFW MCP endpoint ({endpoint}): {e}", file=sys.stderr)
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="BridgeCo BeBoB Virtual UART & Telemetry CLI")
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT, help="ASFW MCP endpoint URL")
    parser.add_argument("--device", type=int, default=1, help="Device instance ID (default: 1)")
    parser.add_argument("--node", type=int, default=0, help="Node ID (default: 0)")
    parser.add_argument("--gen", type=int, default=1, help="Bus generation (default: 1)")

    subparsers = parser.add_subparsers(dest="command", help="Diagnostic command to run")

    # sys stat
    subparsers.add_parser("stat", help="Query streaming statistics (sys stat)")

    # sys avstat
    subparsers.add_parser("avstat", help="Query silicon hardware error latches (sys avstat all)")

    # fw sync show
    subparsers.add_parser("sync", help="Query audio clock synchronization state (fw sync show)")

    # raw shell command
    cmd_parser = subparsers.add_parser("cmd", help="Execute arbitrary shell command")
    cmd_parser.add_argument("shell_cmd", help="Shell command string (e.g. 'os th', 'sys llc')")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    base_args = {
        "deviceInstanceId": args.device,
        "nodeId": args.node,
        "generation": args.gen
    }

    if args.command == "stat":
        res = call_mcp_tool(args.endpoint, "asfw_bebob_get_streaming_stats", base_args)
        content = res.get("content", [{}])[0].get("text", "")
        print(content)
    elif args.command == "avstat":
        res = call_mcp_tool(args.endpoint, "asfw_bebob_get_silicon_status", base_args)
        content = res.get("content", [{}])[0].get("text", "")
        print(content)
    elif args.command == "sync":
        res = call_mcp_tool(args.endpoint, "asfw_bebob_get_sync_state", base_args)
        content = res.get("content", [{}])[0].get("text", "")
        print(content)
    elif args.command == "cmd":
        base_args["command"] = args.shell_cmd
        res = call_mcp_tool(args.endpoint, "asfw_bebob_shell_execute", base_args)
        content = res.get("content", [{}])[0].get("text", "")
        print(content)

if __name__ == "__main__":
    main()
