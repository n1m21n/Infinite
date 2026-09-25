#!/usr/bin/env python3
"""
cli.py
Write to and read L0 from a shell (the MCP server, l0/mcp_server.py, offers the same to a
Claude session as tools).

    python3 l0/cli.py assert "Arrange clip retrigger resets the envelope in Clip::Start" \
        --kind fact --evidence commit:abc1234 --evidence symbol:Clip::Start --confidence 0.7
    python3 l0/cli.py recall "clip retrigger click"     # notes a query would match, weighted
    python3 l0/cli.py list [--all]                       # every active note (--all: retracted too)
    python3 l0/cli.py retract <id> --reason "wrong: the reset is in Lane"
    python3 l0/cli.py approve <id>                       # owner only: makes it owner tier

--owner on assert and the approve command are for the owner's own words. A Claude session
never uses them on its own inference; its notes stay claude/verified tier.
"""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l0.store import KINDS, Store  # noqa: E402


def show(n, store, now):
    ev = ", ".join(e["ref"] + ("" if e["resolves"] else " (?)") for e in n.get("evidence", []))
    state = "" if n.get("active", True) else " [retracted]"
    return (f"{n['id']}  {n['kind']:<8} {n['tier']:<8} w{store.weight(n, now):.2f} "
            f"c{n['confidence']:.2f}{state}\n    {n['claim']}"
            + (f"\n    files: {', '.join(n['files'])}" if n["files"] else "")
            + (f"\n    evidence: {ev}" if ev else ""))


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("assert")
    a.add_argument("claim")
    a.add_argument("--kind", choices=KINDS, default="fact")
    a.add_argument("--evidence", action="append", default=[],
                   help="commit:<hash> | file:<path>[:line] | symbol:<Name> | test:<name> | url:<url>")
    a.add_argument("--file", action="append", default=[], help="a file the claim is about")
    a.add_argument("--confidence", type=float, default=0.6)
    a.add_argument("--session", default="")
    a.add_argument("--owner", action="store_true", help="the owner's own statement")
    r = sub.add_parser("recall")
    r.add_argument("query")
    ls = sub.add_parser("list")
    ls.add_argument("--all", action="store_true")
    rt = sub.add_parser("retract")
    rt.add_argument("id")
    rt.add_argument("--reason", default="")
    ap_ = sub.add_parser("approve")
    ap_.add_argument("id")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    store, now = Store(), time.time()
    if args.cmd == "assert":
        note, created = store.add(args.claim, args.kind, args.evidence, args.confidence,
                                  args.file, session=args.session, owner=args.owner)
        print(("added " if created else "already known ") + show(note, store, now))
    elif args.cmd == "recall":
        hits = store.match(args.query, now)
        print(json.dumps(hits, indent=1) if args.json else
              "\n".join(show(store.notes[h["id"]], store, now) + f"\n    score {h['score']}" for h in hits)
              or "no notes match")
    elif args.cmd == "list":
        notes = list(store.refresh().notes.values()) if args.all else store.active()
        print("\n".join(show(n, store, now) for n in sorted(notes, key=lambda n: n["t"])) or "L0 is empty")
    elif args.cmd == "retract":
        store.retract(args.id, args.reason)
        print(f"retracted {args.id}")
    elif args.cmd == "approve":
        store.approve(args.id)
        print(f"approved {args.id} (owner tier)")


if __name__ == "__main__":
    main()
