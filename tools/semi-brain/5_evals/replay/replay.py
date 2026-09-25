#!/usr/bin/env python3
"""
replay.py
Historical-replay benchmark for the semi-brain: for each past fix (cases.json), build the
brain's index from data that existed before that commit (timeline.py), ask the real engine
(brain_core.SemiBrainCognitiveEngine.analyze_problem) with the commit's own subject/body as
the bug description, and score what it returns against what the commit actually touched.

Ranked outputs scored:
  symbols  frame.ast_impacted_symbols, in order
  files    the file of each of those symbols, then src/ filepaths of the retrieved docs
  region   (main.cpp cases only, additive - not part of gate) whether the best-ranked
           predicted symbol that resolves to a main.cpp region (l4/regions.py) lands in the
           same region as a target symbol; "right file, wrong 30k lines" scores 0 here even
           though file MRR can't tell the difference

Metrics (per query variant, averaged over cases): MRR, recall@{1,5,10,20}.
gate = mean(file MRR, symbol MRR, file R@10, symbol R@10) on the "full" variant.
files_nohub = the file metrics with HUB_FILES dropped from both the ranking and the targets
  (cases whose only target is a hub are skipped). src/main.cpp is edited by most fixes and
  ranked first by most answers, so the plain file MRR mostly measures that; nohub_gate =
  mean(nohub file MRR, nohub file R@10) is what a ranking change should move.

Appends one scorecard line to history.jsonl; per-case detail goes to runs/ (local only).

    python3 replay.py --label baseline            # all cases
    python3 replay.py --label quick --limit 40    # the most recent 40 cases
    python3 replay.py --label X --cases sessions  # real prompts -> files they edited
                                                  # (build_session_cases.py; local only)
    python3 replay.py --label X --cases sessions --grid sets.json --out run.json
                                                  # also score weight sets (l1/sleep.py)

Session cases have no target symbols; their gate is mean(file MRR, file R@10), and by default
SESSION_SAMPLE of them, evenly spread over time, are run so the replay stays at minutes.
"""

import argparse
import contextlib
import io
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from multiprocessing import get_context
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import SEMI_BRAIN_DIR, REPO_PATH  # noqa: E402

sys.path.insert(0, str(SEMI_BRAIN_DIR))
sys.path.insert(0, str(SEMI_BRAIN_DIR / "4_engine"))
sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))

from l4.regions import Regions  # noqa: E402

CASES_FILE = HERE / "cases.json"
STATE_DIR = SEMI_BRAIN_DIR / "l1" / "state"
SESSION_CASES_FILE = STATE_DIR / "session_cases.json"
# Recent-work cutoff for prompt cases: what the live hook would have had (sync lag), not the
# 12 h commit embargo - earlier turns of the same session are exactly what a live brief sees.
SESSION_RECENT_EMBARGO_S = 120.0
SESSION_SAMPLE = 150
HISTORY_FILE = HERE / "history.jsonl"
RUNS_DIR = HERE / "runs"
KS = (1, 5, 10, 20)
HUB_FILES = ("src/main.cpp",)
VARIANTS = ("full", "subject")

_W = {}


def _init_worker(embargo):
    import warnings
    warnings.filterwarnings("ignore")
    from timeline import Timeline
    from l1.vector_cache import VectorCache
    from l3.recent import RecentWork
    _W["timeline"] = tl = Timeline(embargo_hours=embargo)
    _W["cache"] = VectorCache()
    # Outcome-log turns and commits; each case sees only events before its cutoff (run_case).
    turns = []
    if (STATE_DIR / "outcomes.jsonl").exists():
        turns = [json.loads(l) for l in open(STATE_DIR / "outcomes.jsonl")]
    _W["recent"] = RecentWork(turns, [(t, rec["files"]) for t, rec in tl.commits])
    _W["regions"] = Regions()


def rank_metrics(ranked, targets):
    targets = [t.lower() for t in targets]
    ranked = [r.lower() for r in ranked]
    out = {"rr": 0.0}
    for i, r in enumerate(ranked):
        if r in targets:
            out["rr"] = 1.0 / (i + 1)
            break
    for k in KS:
        out[f"r@{k}"] = len(set(ranked[:k]) & set(targets)) / len(targets) if targets else 0.0
    return out


def build_engine(corpora, cache, tmpdir):
    """The production engine, pointed at a throwaway index built from `corpora`."""
    import index_codebase as ic
    from brain_core import SemiBrainCognitiveEngine
    from retriever import HybridRetriever

    docs = ic.prepare_documents(corpora)
    pub = [d for d in docs if d[1] not in ic.PRIVATE_CATEGORIES]
    priv = [d for d in docs if d[1] in ic.PRIVATE_CATEGORIES]
    paths = [Path(tmpdir) / "pub.db", Path(tmpdir) / "priv.db"]
    with contextlib.redirect_stdout(io.StringIO()):
        ic.write_index(paths[0], pub, cache)
        ic.write_index(paths[1], priv, cache)

    engine = SemiBrainCognitiveEngine.__new__(SemiBrainCognitiveEngine)
    engine.schemas = {}
    engine.ast_graph = corpora["ast_data"]
    from l3.network import Network, build_doc_edges
    engine.network = Network(build_doc_edges(corpora), corpora["ast_data"])
    engine.retriever = HybridRetriever()
    engine.retriever.db_paths = paths
    engine.retriever._embed_model = cache
    engine.retriever._load_vector_cache()
    engine.recent = _W.get("recent")
    from l3 import weights
    engine.weights = weights.load()
    return engine, len(docs)


def region_hit(regions, predicted_symbols, target_symbols):
    """None if the case has no target in a known main.cpp region (most cases: main.cpp is a
    target in 87% of prompts, but not every target symbol resolves to a region). Otherwise 1.0
    if the best-ranked predicted symbol that IS in a region lands in one of the target
    region(s), else 0.0 - same-file-different-region ("right file, wrong 2k lines") counts as
    a miss, which is the point: file MRR can't see that distinction, this metric can."""
    target_regions = {r["id"] for r in (regions.region_for_symbol(t) for t in target_symbols) if r}
    if not target_regions:
        return None
    for s in predicted_symbols:
        r = regions.region_for_symbol(s)
        if r:
            return 1.0 if r["id"] in target_regions else 0.0
    return 0.0


def run_case(case):
    tl, cache, regions = _W["timeline"], _W["cache"], _W["regions"]
    t0 = time.perf_counter()
    corpora = tl.corpora_at(case)
    with tempfile.TemporaryDirectory() as tmp:
        engine, ndocs = build_engine(corpora, cache, tmp)
        build_s = time.perf_counter() - t0
        result = {"commit": case["commit"], "subject": case["subject"], "docs": ndocs,
                  "build_s": round(build_s, 2), "variants": {}}
        for v in VARIANTS:
            t1 = time.perf_counter()
            frame = _analyze(engine, case, case[f"query_{v}"])
            lat = time.perf_counter() - t1
            syms = list(frame.ast_impacted_symbols)
            files = answer_files(engine, frame)
            result["variants"][v] = {
                "latency_ms": round(lat * 1000, 1), **file_metrics(files, case["target_files"]),
                "symbols": rank_metrics(syms, case["target_symbols"]) if case["target_symbols"] else None,
                "region": region_hit(regions, syms, case["target_symbols"]),
                "top_files": files[:10], "top_symbols": syms[:10],
            }
        # Weight sets for the sleep job (l1/sleep.py --grid): the full query again with each
        # set, on the engine already built for this case - a grid point costs one query.
        if case.get("grid"):
            base = dict(engine.weights)
            result["grid"] = []
            for g in case["grid"]:
                engine.weights = dict(base, **g)
                files = answer_files(engine, _analyze(engine, case, case["query_full"]))
                result["grid"].append(file_metrics(files, case["target_files"]))
            engine.weights = base
    return result


def _analyze(engine, case, query):
    return engine.analyze_problem(query, now=case["time"], session=case.get("session", ""),
                                  embargo=case["recent_embargo"])


def answer_files(engine, frame):
    """The engine's own ranked file list (L3 onwards); after it, the files of the matched
    symbols then of the retrieved docs."""
    files = list(frame.ranked_files)
    for s in frame.ast_impacted_symbols:
        f = engine._get_symbol_meta(s).get("file", "")
        if f and f not in files:
            files.append(f)
    for h in frame.retrieved_context:
        f = h.get("filepath", "") or ""
        if f.startswith("src/") and f not in files:
            files.append(f)
    return files


def file_metrics(files, targets):
    return {"files": rank_metrics(files, targets),
            "files_nohub": rank_metrics([f for f in files if f not in HUB_FILES],
                                        [f for f in targets if f not in HUB_FILES])
                           if set(targets) - set(HUB_FILES) else None}


def summarize(results):
    out = {}
    for v in VARIANTS:
        rows = [r["variants"][v] for r in results]
        block = {}
        for kind in ("files", "files_nohub", "symbols"):
            ms = [r.get(kind) for r in rows if r.get(kind) is not None]
            if not ms:
                block[kind] = None
                continue
            block[kind] = {"n": len(ms), "mrr": round(statistics.mean(m["rr"] for m in ms), 4)}
            for k in KS:
                block[kind][f"r@{k}"] = round(statistics.mean(m[f"r@{k}"] for m in ms), 4)
        lats = sorted(r["latency_ms"] for r in rows)
        block["latency_ms"] = {"p50": lats[len(lats) // 2], "p95": lats[int(len(lats) * 0.95) - 1],
                               "mean": round(statistics.mean(lats), 1)}
        regions = [r.get("region") for r in rows if r.get("region") is not None]
        block["region_hit"] = {"n": len(regions),
                               "rate": round(statistics.mean(regions), 4)} if regions else None
        out[v] = block
    f, s = out["full"]["files"], out["full"]["symbols"]
    h = out["full"]["files_nohub"]
    out["nohub_gate"] = round((h["mrr"] + h["r@10"]) / 2, 4) if h else None
    if s is None:
        out["gate"] = round((f["mrr"] + f["r@10"]) / 2, 4)
    else:
        out["gate"] = round((f["mrr"] + s["mrr"] + f["r@10"] + s["r@10"]) / 4, 4)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--limit", type=int, default=0, help="only the most recent N cases")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--embargo-hours", type=float, default=12.0)
    ap.add_argument("--no-history", action="store_true")
    ap.add_argument("--cases", choices=("commits", "sessions"), default="commits")
    ap.add_argument("--sample", type=int, default=None,
                    help=f"evenly spaced sample of N cases (sessions default {SESSION_SAMPLE})")
    ap.add_argument("--grid", help="JSON file: a list of weight sets (l3/weights.py keys) to "
                                   "score on each case as well (l1/sleep.py)")
    ap.add_argument("--out", help="also write the run (card + cases) to this file")
    args = ap.parse_args()

    if args.cases == "sessions":
        cases = json.loads(SESSION_CASES_FILE.read_text())
        if args.sample is None:
            args.sample = SESSION_SAMPLE
    else:
        cases = json.loads(CASES_FILE.read_text())
    if args.limit:
        cases = cases[-args.limit:]
    if args.sample and args.sample < len(cases):
        step = len(cases) / args.sample
        cases = [cases[int(i * step)] for i in range(args.sample)]
    recent_embargo = SESSION_RECENT_EMBARGO_S if args.cases == "sessions" else args.embargo_hours * 3600.0
    grid = json.loads(Path(args.grid).read_text()) if args.grid else None
    cases = [dict(c, recent_embargo=recent_embargo, grid=grid) for c in cases]
    t0 = time.time()
    ctx = get_context("spawn")
    with ctx.Pool(args.jobs, initializer=_init_worker, initargs=(args.embargo_hours,)) as pool:
        results = []
        for i, r in enumerate(pool.imap(run_case, cases), 1):
            results.append(r)
            fr = r["variants"]["full"]["files"]["rr"]
            print(f"[{i}/{len(cases)}] rr_file={fr:.2f} build={r['build_s']}s {r['subject'][:60]}", flush=True)
    wall = time.time() - t0

    summary = summarize(results)
    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO_PATH,
                          capture_output=True, text=True).stdout.strip()
    card = {"kind": "replay" if args.cases == "commits" else "replay-sessions", "when": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "label": args.label, "head": head, "cases": len(results),
            "embargo_hours": args.embargo_hours, "wall_s": round(wall, 1),
            "mean_build_s": round(statistics.mean(r["build_s"] for r in results), 2), **summary}
    RUNS_DIR.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    run = json.dumps({"card": card, "cases": results}, indent=1)
    (RUNS_DIR / f"{stamp}-{args.label}.json").write_text(run)
    if args.out:
        Path(args.out).write_text(run)
    if not args.no_history:
        with open(HISTORY_FILE, "a") as f:
            f.write(json.dumps(card) + "\n")
    print(json.dumps(card, indent=1))


if __name__ == "__main__":
    main()
