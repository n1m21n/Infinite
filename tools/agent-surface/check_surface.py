#!/usr/bin/env python3
"""Agent surface audit: is every `infinite` verb documented, every MCP tool listed, every node findable by its own words?

Three checks, one report (the numbers go into docs/release/v2.0-agent-surface.md):

  verbs   every top-level verb the `infinite` command dispatches (src/core/InfiniteCli.cpp) appears in its own
          help text (kHelp) and in docs/headless/GUIDE.md. Exit 1 on any gap.
  mcp     every tool registered in tools/npm/infinite-engine/lib/server.js is named in that package's README.
          Exit 1 on any gap.
  nodes   coverage of node types. The list is src/core/chat/NodeVocab.gen.h (every node the app describes, as of
          the last generator run; no build needed). Counts how many have a Node Field Guide card (plain-language
          description: tools/manual/cards_base.json + cards.py + cards_v06.py). With --eval, also how many are reached
          by a passing ask in tests/chat/find_nodes.tsv; the eval comes from the finder binary:
          `find-eval tests/chat/find_nodes.tsv > evalout.txt` (needs a built app, see the release doc).

  python3 tools/agent-surface/check_surface.py verbs mcp
  python3 tools/agent-surface/check_surface.py nodes [--nodes other.json] [--eval evalout.txt]

Exit codes: 0 pass, 1 a gap was found, 2 usage or missing input.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "manual"))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def check_verbs():
    cli = read("src/core/InfiniteCli.cpp")
    dispatched = sorted(set(re.findall(r'verb == "([a-z0-9-]+)"', cli)))
    help_text = cli[cli.index("kHelp = R"):]
    help_text = help_text[:help_text.index(")\";")]
    guide = read("docs/headless/GUIDE.md")
    gaps = []
    for v in dispatched:
        if not re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(v), help_text):
            gaps.append((v, "not in `infinite` help text"))
        if not re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(v), guide):
            gaps.append((v, "not in docs/headless/GUIDE.md"))
    print("verbs: %d dispatched, %d gaps" % (len(dispatched), len(gaps)))
    for v, why in gaps:
        print("  %-14s %s" % (v, why))
    return 1 if gaps else 0


def check_mcp():
    pkg = os.path.join(ROOT, "tools", "npm", "infinite-engine")
    server = open(os.path.join(pkg, "lib", "server.js"), encoding="utf-8").read()
    readme = open(os.path.join(pkg, "README.md"), encoding="utf-8").read()
    tools = sorted(set(re.findall(r'name: "(infinite_[a-z0-9_]+)"', server)))
    gaps = [t for t in tools if "`%s`" % t not in readme]
    print("mcp: %d tools registered, %d missing from README" % (len(tools), len(gaps)))
    for t in gaps:
        print("  " + t)
    return 1 if gaps else 0


def norm(s):
    return "".join(ch for ch in s.lower() if ch.isalnum())


def vocab_types():
    """Every node type in NodeVocab.gen.h (one entry per type, generated from `infinite describe`)."""
    src = read("src/core/chat/NodeVocab.gen.h")
    return sorted({m for m in re.findall(r'\{ "((?:[^"\\]|\\.)*)", "[^"]*", "[^"]*", "(?:[^"\\]|\\.)*" \}', src)})


def check_nodes(args):
    import cards  # noqa: E402  (tools/manual/cards.py holds the guide chapters)
    import cards_v06  # noqa: E402  (chapters 27-28, with the everyday cards)
    base = json.load(open(os.path.join(ROOT, "tools", "manual", "cards_base.json"), encoding="utf-8"))
    names = {norm(c[0]) for ch in base + cards.CHAPTERS + cards_v06.CHAPTERS_V06 for c in ch["cards"]}
    if args.nodes:
        d = json.load(open(args.nodes, encoding="utf-8"))
        types = sorted({t for v in d.values() for t in v})
    else:
        types = vocab_types()
    carded = [t for t in types if norm(t) in names]
    print("nodes: %d types; %d with a guide card (%d%%); %d without" %
          (len(types), len(carded), 100 * len(carded) // max(1, len(types)), len(types) - len(carded)))
    if args.eval:
        asks = {}
        for line in open(os.path.join(ROOT, "tests", "chat", "find_nodes.tsv"), encoding="utf-8"):
            if line.startswith("#") or "\t" not in line:
                continue
            ask, want = line.rstrip("\n").split("\t", 1)
            asks[ask.strip()] = [w.strip() for w in want.split("|")]
        missed = set()
        for line in open(args.eval, encoding="utf-8"):
            if line.startswith("miss"):
                missed.add(line[6:].split("  ->")[0].strip())
        reached = set()
        for ask, want in asks.items():
            if ask not in missed:
                reached.update(norm(w) for w in want)
        found = [t for t in types if norm(t) in reached]
        print("finder: %d asks, %d missed; %d of %d node types reached by a passing ask (%d%%)" %
              (len(asks), len(missed & set(asks)), len(found), len(types), 100 * len(found) // max(1, len(types))))
        print("  unreached: " + ", ".join(t for t in types if norm(t) not in reached))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("checks", nargs="*", choices=["verbs", "mcp", "nodes"], default=["verbs", "mcp"])
    ap.add_argument("--nodes", help="JSON from `Infinite --cli nodes`")
    ap.add_argument("--eval", help="output of find-eval on tests/chat/find_nodes.tsv")
    args = ap.parse_args()
    rc = 0
    for c in args.checks or ["verbs", "mcp"]:
        if c == "verbs":
            rc = max(rc, check_verbs())
        elif c == "mcp":
            rc = max(rc, check_mcp())
        elif c == "nodes":
            rc = max(rc, check_nodes(args))
    return rc


if __name__ == "__main__":
    sys.exit(main())
