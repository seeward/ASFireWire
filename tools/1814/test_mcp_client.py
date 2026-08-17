#!/usr/bin/env python3
import json
import urllib.request
import urllib.error
import sys

ENDPOINT = "http://127.0.0.1:8766/mcp"

def send_rpc(payload, headers_extra=None):
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream"
    }
    if headers_extra:
        headers.update(headers_extra)
        
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(ENDPOINT, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            resp_headers = dict(resp.headers)
            body = resp.read().decode("utf-8")
            return resp.status, resp_headers, body
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8") if e.fp else ""
        return e.code, dict(e.headers), body
    except Exception as e:
        return 0, {}, str(e)

def main():
    print(f"Connecting to {ENDPOINT}...")
    
    # 1. Initialize
    init_payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "ASFWTestClient",
                "version": "1.0.0"
            }
        }
    }
    status, headers, body = send_rpc(init_payload)
    print(f"[Init] Status: {status}")
    print(f"[Init] Headers: {headers}")
    print(f"[Init] Response: {body}")
    
    session_id = None
    for k, v in headers.items():
        if k.lower() == "mcp-session-id":
            session_id = v
            break

    print(f"[Session ID]: {session_id}")
    session_headers = {"Mcp-Session-Id": session_id} if session_id else {}

    # 2. Initialized notification
    notif_payload = {
        "jsonrpc": "2.0",
        "method": "notifications/initialized"
    }
    send_rpc(notif_payload, session_headers)

    # 3. List tools
    list_payload = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    }
    status, headers, body = send_rpc(list_payload, session_headers)
    
    # Parse SSE message data
    for line in body.splitlines():
        if line.startswith("data: "):
            try:
                data = json.loads(line[6:])
                tools = data.get("result", {}).get("tools", [])
                print(f"Total tools registered: {len(tools)}")
                tool_names = [t["name"] for t in tools]
                print(f"Tool names: {tool_names[:10]} ...")
            except Exception as e:
                print("Parse err:", e)

    # 4. Call asfw_list_nodes
    nodes_payload = {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "asfw_list_nodes",
            "arguments": {}
        }
    }
    status, headers, body = send_rpc(nodes_payload, session_headers)
    print(f"\n[asfw_list_nodes Result]:\n{body}")

    # 5. Call asfw_get_topology
    topo_payload = {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "tools/call",
        "params": {
            "name": "asfw_get_topology",
            "arguments": {}
        }
    }
    status, headers, body = send_rpc(topo_payload, session_headers)
    print(f"\n[asfw_get_topology Result]:\n{body}")

if __name__ == "__main__":
    main()
