#!/usr/bin/env python3
"""Minimal MCP-over-HTTP client for checking Infinite's in-app server.
usage: mcp_http_probe.py PORT TOKEN [tool [json-args]]"""
import json, sys, urllib.request, urllib.error

port, token = sys.argv[1], sys.argv[2]
url = f"http://127.0.0.1:{port}/mcp"

def rpc(method, params=None, id=1):
    body = json.dumps({"jsonrpc": "2.0", "id": id, "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(url, body, {"Content-Type": "application/json", "Authorization": "Bearer " + token})
    try:
        return json.load(urllib.request.urlopen(req, timeout=60))
    except urllib.error.HTTPError as e:
        return {"http_error": e.code}

print("initialize:", json.dumps(rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "probe", "version": "0"}}))[:300])
tools = rpc("tools/list")
print("tools:", [t["name"] for t in tools["result"]["tools"]])
name = sys.argv[3] if len(sys.argv) > 3 else "infinite_context"
args = json.loads(sys.argv[4]) if len(sys.argv) > 4 else {}
print(name, "->", json.dumps(rpc("tools/call", {"name": name, "arguments": args}))[:1500])
