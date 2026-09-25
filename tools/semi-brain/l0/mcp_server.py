#!/usr/bin/env python3
"""
mcp_server.py
A minimal MCP server (stdio, JSON-RPC 2.0, one message per line, stdlib only) that gives a
Claude session the brain as tools:

  brain_brief    the ~200-token brief for a question, from the daemon's warm server
                 (l5/serve.py); no engine is loaded in this process
  brain_recall   the L0 notes a question matches, with their weights
  brain_assert   add an L0 note (claim, kind, evidence, confidence). Always claude or
                 verified tier: owner tier is only ever set by the owner (l0/cli.py approve)
  brain_retract  withdraw a note that turned out wrong (not an owner note)

Registered in the repo's .mcp.json as "semi-brain".
"""

import json
import subprocess
import sys
from pathlib import Path

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SEMI_BRAIN_DIR))
from l0.store import KINDS, Store  # noqa: E402

SERVER = {"name": "semi-brain", "version": "0.1"}
DEFAULT_PROTOCOL = "2025-06-18"

TOOLS = [
    {"name": "brain_brief",
     "description": "Infinite's semi-brain brief for a bug/feature question: likely files with the past "
                    "fix or chat behind each, symbols with file:line, area, past fixes, skills to load, "
                    "matching L0 notes. A lead, not a fact.",
     "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "brain_recall",
     "description": "L0 notes (claims, decisions, rejected alternatives, open questions, hints) earlier "
                    "sessions left that match a question, with trust tier and weight.",
     "inputSchema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}},
    {"name": "brain_assert",
     "description": "Leave a note for later sessions: a fact you verified, a decision and its reason, an "
                    "alternative you rejected and why, an open question, or where to look for what. "
                    "Give evidence (commit:<hash>, file:<path>[:line], symbol:<Name>, test:<name>, url:<url>); "
                    "notes whose evidence resolves rank higher. Only assert what this session established, "
                    "never a guess; the same claim twice changes nothing.",
     "inputSchema": {"type": "object", "properties": {
         "claim": {"type": "string"},
         "kind": {"type": "string", "enum": list(KINDS)},
         "evidence": {"type": "array", "items": {"type": "string"}},
         "files": {"type": "array", "items": {"type": "string"}},
         "confidence": {"type": "number", "minimum": 0, "maximum": 1}},
         "required": ["claim", "kind", "evidence", "confidence"]}},
    {"name": "brain_retract",
     "description": "Withdraw an L0 note that turned out wrong.",
     "inputSchema": {"type": "object", "properties": {"id": {"type": "string"}, "reason": {"type": "string"}},
                     "required": ["id", "reason"]}},
]


def git_common_dir():
    out = subprocess.run(["git", "rev-parse", "--git-common-dir"], cwd=SEMI_BRAIN_DIR,
                         capture_output=True, text=True).stdout.strip()
    return (SEMI_BRAIN_DIR / out).resolve() if out else None


def call(name, args, store):
    if name == "brain_brief":
        from l5.serve import ask
        gitdir = git_common_dir()
        try:
            reply = ask(gitdir / "brain_brief.sock", args["query"], session="mcp")
        except OSError as e:
            return f"brief server unavailable ({e}); is the semi-brain daemon running?", True
        return reply.get("brief") or "no brief", False
    if name == "brain_recall":
        hits = store.match(args["query"])
        return json.dumps(hits, indent=1) if hits else "no notes match", False
    if name == "brain_assert":
        note, created = store.add(args["claim"], args.get("kind", "fact"), args.get("evidence", []),
                                  args.get("confidence", 0.6), args.get("files", []),
                                  owner=False, source="mcp")
        ev = ", ".join(e["ref"] + (" ok" if e["resolves"] else " unresolved") for e in note["evidence"])
        return (f"{'added' if created else 'already known'} {note['id']} ({note['tier']} tier)"
                + (f"; evidence: {ev}" if ev else "")), False
    if name == "brain_retract":
        n = store.refresh().notes.get(args["id"])
        if n is None:
            return f"no note {args['id']}", True
        if n["tier"] == "owner":
            return "owner notes are retracted by the owner (l0/cli.py retract)", True
        store.retract(args["id"], args.get("reason", ""))
        return f"retracted {args['id']}", False
    raise KeyError(name)


def handle(msg, store):
    method, mid = msg.get("method"), msg.get("id")
    if mid is None:
        return None  # notification
    if method == "initialize":
        proto = (msg.get("params") or {}).get("protocolVersion") or DEFAULT_PROTOCOL
        result = {"protocolVersion": proto, "capabilities": {"tools": {}}, "serverInfo": SERVER}
    elif method == "ping":
        result = {}
    elif method == "tools/list":
        result = {"tools": TOOLS}
    elif method == "tools/call":
        p = msg.get("params") or {}
        try:
            text, is_error = call(p.get("name"), p.get("arguments") or {}, store)
        except KeyError as e:
            return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": f"unknown tool or argument {e}"}}
        except (ValueError, TypeError) as e:
            text, is_error = str(e), True
        result = {"content": [{"type": "text", "text": text}], "isError": is_error}
    else:
        return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"no method {method}"}}
    return {"jsonrpc": "2.0", "id": mid, "result": result}


def main():
    store = Store()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            reply = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}}
        else:
            reply = handle(msg, store)
        if reply is not None:
            sys.stdout.write(json.dumps(reply) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
