#!/usr/bin/env python3
"""
sleep.py
Nightly consolidation: what the day's prompts and edits teach the brain, gated like any other
brain change. Run by the launchd agent com.infinite.semi-brain.sleep (4_engine/install_hooks.sh).

  1 outcomes  update the outcome log (l1/outcomes.py) and rebuild the prompt cases
  2 retune    the prompt-case replay once, with a grid around the current l3/weights.py set
              (each weight halved and raised, one at a time; a grid point costs one query per
              case, on the engine already built for it). The best set on the even cases (nohub
              score) replaces the current one only if it also beats it on the odd cases by
              MIN_GAIN and does not lower the with-main.cpp file score over all cases
              -> l1/state/learned_weights.json. Commit cases are unaffected: the weights only
              apply with a session (brain_core.analyze_problem)
  3 credit    every brief that showed an L0 note (l1/state/briefs.jsonl) joined with its turn
              (5_evals/live.py): the turn edited one of the note's files = hit, edited other
              files = miss, edited nothing = no evidence. Counts per (region, kind) and per
              confidence bin -> l1/state/l0_reliability.json, recomputed from scratch
  4 propose   files the current weights missed (not in the top 10) on >= PROPOSE_MIN prompt
              cases become hint proposals - the words those prompts share that are also words
              of the code's identifiers, ranked by tf-idf, and the file - in
              l1/state/proposals.jsonl. Nothing reaches the brain until the owner approves one
              (it becomes an owner-tier L0 note); a rejected one is never proposed again
  guard       skipped while Infinite runs or the watch daemon is paused (a benchmark is
              measuring); --force overrides
  log         one card per night in l1/state/sleep.jsonl

    python3 l1/sleep.py                   # the nightly run
    python3 l1/sleep.py --proposals       # pending proposals
    python3 l1/sleep.py --approve ID      # owner: proposal -> owner-tier L0 note
    python3 l1/sleep.py --reject ID
"""

import argparse
import importlib.util
import json
import math
import os
import subprocess
import sys
import time
import uuid
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import SEMI_BRAIN_DIR, STATE_DIR  # noqa: E402
from l0.store import CAP, RELIABILITY, Store, region, words  # noqa: E402
from l3 import weights as W  # noqa: E402

LEARNED = W.LEARNED
PROPOSALS = STATE_DIR / "proposals.jsonl"
LOG = STATE_DIR / "sleep.jsonl"
REPLAY = SEMI_BRAIN_DIR / "5_evals" / "replay"
DAEMON = "com.infinite.semi-brain.watchd"
HUB_FILES = ("src/main.cpp",)

STEPS = {"session_w": (0.5, 1.5), "recent_w": (0.5, 2.0), "tau_days": (0.5, 2.0)}
MIN_GAIN = 0.005
PROPOSE_MIN, PROPOSE_MAX, PROPOSE_WORDS = 3, 5, 5
CAL_BINS = ((0.0, 0.4), (0.4, 0.6), (0.6, 0.8), (0.8, 1.01))


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def busy():
    """Why sleep should not run now, or ''."""
    if subprocess.run(["pgrep", "-f", "Infinite.app/Contents/MacOS/"], capture_output=True).returncode == 0:
        return "Infinite is running"
    r = subprocess.run(["launchctl", "print", f"gui/{os.getuid()}/{DAEMON}"], capture_output=True)
    if r.returncode != 0:
        return "the watch daemon is paused (a benchmark?)"
    return ""


# ---- 1 outcomes -------------------------------------------------------------------------
def refresh_outcomes():
    sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))
    from l1.outcomes import update
    counts = update()
    subprocess.run([sys.executable, str(REPLAY / "build_session_cases.py")], check=True,
                   capture_output=True)
    return counts


# ---- 2 retune ---------------------------------------------------------------------------
def grid_around(cur):
    sets = [dict(cur)]
    for k, factors in STEPS.items():
        for f in factors:
            sets.append(dict(cur, **{k: round(cur[k] * f, 4)}))
    return sets


def _score(rows, key):
    rows = [r[key] for r in rows if r.get(key)]
    if not rows:
        return 0.0, 0
    return sum(r["rr"] + r["r@10"] for r in rows) / (2 * len(rows)), len(rows)


def retune(run):
    """(best set or None, table of every set's scores)."""
    cases = run["cases"]
    n = len(cases[0]["grid"])
    table = []
    for g in range(n):
        rows = [c["grid"][g] for c in cases]
        even, odd = rows[0::2], rows[1::2]
        table.append({"even": round(_score(even, "files_nohub")[0], 4),
                      "odd": round(_score(odd, "files_nohub")[0], 4),
                      "all_files": round(_score(rows, "files")[0], 4),
                      "all_nohub": round(_score(rows, "files_nohub")[0], 4)})
    best = max(range(n), key=lambda g: table[g]["even"])
    cur = table[0]
    if best != 0 and table[best]["odd"] >= cur["odd"] + MIN_GAIN and table[best]["all_files"] >= cur["all_files"]:
        return best, table
    return None, table


# ---- 3 credit ---------------------------------------------------------------------------
def _live():
    spec = importlib.util.spec_from_file_location("live", SEMI_BRAIN_DIR / "5_evals" / "live.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def credit(store):
    live = _live()
    briefs = [b for b in live.load("briefs.jsonl") if b.get("notes")]
    pairs = live.join(briefs, live.load("outcomes.jsonl"))
    regions, bins = defaultdict(lambda: [0, 0]), defaultdict(lambda: [0, 0])
    notes = store.refresh().notes
    for b, turn in pairs:
        edited = set(turn["edited"])
        if not edited:
            continue
        for nid in b["notes"]:
            n = notes.get(nid)
            if n is None or not n["files"]:
                continue
            hit = bool(edited & set(n["files"]))
            regions[f"{region(n['files'])}|{n['kind']}"][0 if hit else 1] += 1
            for lo, hi in CAL_BINS:
                if lo <= n["confidence"] < hi:
                    bins[(lo, hi)][0 if hit else 1] += 1
    cal = [[lo, hi, round(s / (s + f), 3), s + f] for (lo, hi), (s, f) in sorted(bins.items())]
    out = {"t": time.time(), "regions": dict(regions), "calibration": cal}
    RELIABILITY.write_text(json.dumps(out, indent=1))
    return {"pairs": len(pairs), "credited": sum(s + f for s, f in regions.values())}


# ---- 4 propose --------------------------------------------------------------------------
def load_proposals():
    if not PROPOSALS.exists():
        return {}
    out = {}
    for line in PROPOSALS.open():
        e = json.loads(line)
        if e.get("op") == "propose":
            out[e["id"]] = dict(e, status="pending")
        elif e.get("id") in out:
            out[e["id"]]["status"] = e["op"]
    return out


def _append(event):
    with PROPOSALS.open("a") as fh:
        fh.write(json.dumps(event) + "\n")


def code_vocabulary():
    """Words of the code's own identifiers: a hint names what the code calls things, not the
    prose around a request ("rather", "directly")."""
    from l0.store import AST_GRAPH
    try:
        syms = json.loads(AST_GRAPH.read_text()).get("symbols", {})
    except (OSError, ValueError):
        return set()
    return {w for name in syms for w in words(name.replace("::", " "))}


def propose(run, cases_by_id, store):
    missed = defaultdict(list)
    for c in run["cases"]:
        case = cases_by_id.get(c["commit"])
        if case is None:
            continue
        top = set(c["variants"]["full"]["top_files"])
        for f in case["target_files"]:
            if f not in HUB_FILES and f not in top:
                missed[f].append(case["query_full"])
    df, total = Counter(), len(cases_by_id)
    for case in cases_by_id.values():
        df.update(words(case["query_full"]))
    vocab = code_vocabulary()
    known = {f for p in load_proposals().values() for f in p["files"]}
    known |= {f for n in store.active() if n["kind"] == "hint" for f in n["files"]}
    new = []
    for f, prompts in sorted(missed.items(), key=lambda kv: -len(kv[1])):
        if len(prompts) < PROPOSE_MIN or f in known:
            continue
        cnt = Counter(w for p in prompts for w in words(p))
        shared = [w for w, k in cnt.items() if k >= 2 and w in vocab]
        shared.sort(key=lambda w: -cnt[w] * math.log(total / df[w]))
        if not shared:
            continue
        e = {"op": "propose", "id": uuid.uuid4().hex[:8], "t": time.time(), "kind": "hint",
             "claim": f"Prompts about {', '.join(shared[:PROPOSE_WORDS])} often end in edits to {f}",
             "files": [f], "support": len(prompts)}
        _append(e)
        new.append(e)
        if len(new) >= PROPOSE_MAX:
            break
    return new


def decide(pid, approve):
    props = load_proposals()
    p = props.get(pid)
    if p is None or p["status"] != "pending":
        sys.exit(f"no pending proposal {pid}")
    if approve:
        note, _ = Store().add(p["claim"], p["kind"], [f"file:{f}" for f in p["files"]], 0.8,
                              p["files"], owner=True, source=f"sleep:{pid}")
        print(f"approved {pid} -> L0 note {note['id']} (owner tier, weight <= {CAP})")
    else:
        print(f"rejected {pid}")
    _append({"op": "approved" if approve else "rejected", "id": pid, "t": time.time()})


# ---- the night --------------------------------------------------------------------------
def night(force=False, jobs=3, sample=0):
    t0 = time.time()
    card = {"when": time.strftime("%Y-%m-%dT%H:%M:%S")}
    why = "" if force else busy()
    if why:
        card["skipped"] = why
        log(f"skipped: {why}")
    else:
        card["outcomes"] = refresh_outcomes()
        log(f"outcomes {card['outcomes']}")
        cur = W.load()
        sets = grid_around(cur)
        tmp = STATE_DIR / "sleep_grid.json"
        out = STATE_DIR / "sleep_run.json"
        tmp.write_text(json.dumps(sets))
        subprocess.run([sys.executable, "-W", "ignore", str(REPLAY / "replay.py"), "--label", "sleep",
                        "--cases", "sessions", "--sample", str(sample), "--no-history", "--jobs", str(jobs),
                        "--grid", str(tmp), "--out", str(out)], check=True, capture_output=True)
        run = json.loads(out.read_text())
        best, table = retune(run)
        card["grid"] = [dict(s, **t) for s, t in zip(sets, table)]
        if best is not None:
            LEARNED.write_text(json.dumps({"weights": sets[best], "t": time.time(),
                                           "from": cur, "scores": table[best], "was": table[0]}, indent=1))
            card["kept"] = sets[best]
            log(f"weights {cur} -> {sets[best]}")
        else:
            log("weights unchanged")
        store = Store()
        card["credit"] = credit(store)
        cases = {c["commit"]: c for c in json.loads((STATE_DIR / "session_cases.json").read_text())}
        card["proposed"] = [p["id"] for p in propose(run, cases, store)]
        log(f"credit {card['credit']}, {len(card['proposed'])} new proposals")
    card["wall_s"] = round(time.time() - t0, 1)
    with LOG.open("a") as fh:
        fh.write(json.dumps(card) + "\n")
    return card


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="run even while Infinite or a benchmark is")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--sample", type=int, default=0, help="prompt cases to replay (0: all)")
    ap.add_argument("--proposals", action="store_true")
    ap.add_argument("--approve")
    ap.add_argument("--reject")
    args = ap.parse_args()
    if args.proposals:
        pending = [p for p in load_proposals().values() if p["status"] == "pending"]
        for p in pending:
            print(f"{p['id']}  ({p['support']} prompts)  {p['claim']}")
        print(f"{len(pending)} pending" if pending else "no pending proposals")
    elif args.approve or args.reject:
        decide(args.approve or args.reject, bool(args.approve))
    else:
        print(json.dumps(night(args.force, args.jobs, args.sample), indent=1))


if __name__ == "__main__":
    main()
