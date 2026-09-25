#!/usr/bin/env python3
"""
live.py
The live scorecard: every brief the prompt hook served (l1/state/briefs.jsonl) against what
that turn then actually read and edited (l1/state/outcomes.jsonl, l1/outcomes.py). Unlike the
replay, nothing here is simulated - these are real prompts and real outcomes.

  matched   a brief joins the turn of the same session that started within JOIN_S of it
  hit@k     the turn edited at least one of the brief's first k files
  recall    edited files that were in the brief / edited files
  read      the same for files the turn only read
  nohub     the same with src/main.cpp dropped from both sides (see replay.py HUB_FILES)

    python3 5_evals/live.py            # print the card
    python3 5_evals/live.py --days 7   # recent briefs only
"""

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
STATE = SEMI_BRAIN_DIR / "l1" / "state"
JOIN_S = 120.0
HUB_FILES = ("src/main.cpp",)
MAIN_CPP = "src/main.cpp"


def load(name):
    p = STATE / name
    return [json.loads(l) for l in p.open()] if p.exists() else []


def join(briefs, turns):
    by_session = {}
    for t in turns:
        by_session.setdefault(t["session"], []).append(t)
    pairs = []
    for b in briefs:
        cands = [t for t in by_session.get(b.get("session", ""), []) if abs(t["t"] - b["t"]) <= JOIN_S]
        if cands:
            pairs.append((b, min(cands, key=lambda t: abs(t["t"] - b["t"]))))
    return pairs


def score(pairs, key, drop=()):
    rows = []
    for b, t in pairs:
        got = [f for f in t[key] if f not in drop]
        if not got:
            continue
        files = [f for f in b["files"] if f not in drop]
        rows.append({"hit@1": float(bool(set(files[:1]) & set(got))),
                     "hit@4": float(bool(set(files[:4]) & set(got))),
                     "recall": len(set(files) & set(got)) / len(set(got))})
    if not rows:
        return {"n": 0}
    return {"n": len(rows), **{k: round(statistics.mean(r[k] for r in rows), 3) for k in rows[0]}}


def touched_regions(regions, commit_hashes):
    """Region ids touched by `commit_hashes`' diffs to main.cpp - real git diff hunks, not a
    guess, so this only covers turns that actually committed (outcomes.jsonl has no line-level
    data for uncommitted edits)."""
    sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))
    from build_main_cpp_regions import touched_lines
    rids = set()
    for h in commit_hashes:
        for line in touched_lines(h):
            r = regions.region_for_line(line)
            if r:
                rids.add(r["id"])
    return rids


def score_regions(pairs):
    """hit@1/hit@2 for main.cpp: did the brief's top-ranked (then top-2) region match a region
    a committed turn actually touched. Only turns with commits are scorable (see
    `touched_regions`); n is usually much smaller than the file-level scores above."""
    from l4.regions import Regions
    regions = Regions()
    rows = []
    for b, t in pairs:
        main_regions = b.get("main_regions") or []
        if not main_regions or not t.get("commits") or MAIN_CPP not in t.get("edited", []):
            continue
        got = touched_regions(regions, t["commits"])
        if not got:
            continue
        rows.append({"hit@1": float(main_regions[0] in got),
                     "hit@2": float(bool(set(main_regions[:2]) & got))})
    if not rows:
        return {"n": 0}
    return {"n": len(rows), **{k: round(statistics.mean(r[k] for r in rows), 3) for k in rows[0]}}


def card(days=None):
    briefs, turns = load("briefs.jsonl"), load("outcomes.jsonl")
    if days:
        cut = time.time() - days * 86400
        briefs = [b for b in briefs if b["t"] >= cut]
    pairs = join(briefs, turns)
    return {"briefs": len(briefs), "matched": len(pairs),
            "median_ms": statistics.median(b["ms"] for b in briefs) if briefs else None,
            "edited": score(pairs, "edited"), "edited_nohub": score(pairs, "edited", HUB_FILES),
            "read_nohub": score(pairs, "read", HUB_FILES), "regions": score_regions(pairs)}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=float)
    args = ap.parse_args()
    sys.path.insert(0, str(SEMI_BRAIN_DIR))
    print(json.dumps(card(args.days), indent=1))
