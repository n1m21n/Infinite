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

sys.path.insert(0, str(SEMI_BRAIN_DIR / "4_engine"))
sys.path.insert(0, str(SEMI_BRAIN_DIR / "1_extractors"))

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
    return engine, len(docs)


def run_case(case):
    tl, cache = _W["timeline"], _W["cache"]
    t0 = time.perf_counter()
    corpora = tl.corpora_at(case)
    with tempfile.TemporaryDirectory() as tmp:
        engine, ndocs = build_engine(corpora, cache, tmp)
        build_s = time.perf_counter() - t0
        result = {"commit": case["commit"], "subject": case["subject"], "docs": ndocs,
                  "build_s": round(build_s, 2), "variants": {}}
        for v in VARIANTS:
            q = case[f"query_{v}"]
            t1 = time.perf_counter()
            frame = engine.analyze_problem(q, now=case["time"], session=case.get("session", ""),
                                           embargo=case["recent_embargo"])
            lat = time.perf_counter() - t1
            syms = list(frame.ast_impacted_symbols)
            # The engine's own ranked file list (L3 onwards); before it, the files of the
            # matched symbols then of the retrieved docs.
            files = list(frame.ranked_files)
            for s in syms:
                f = engine._get_symbol_meta(s).get("file", "")
                if f and f not in files:
                    files.append(f)
            for h in frame.retrieved_context:
                f = h.get("filepath", "") or ""
                if f.startswith("src/") and f not in files:
                    files.append(f)
            result["variants"][v] = {
                "latency_ms": round(lat * 1000, 1),
                "files": rank_metrics(files, case["target_files"]),
                "files_nohub": rank_metrics([f for f in files if f not in HUB_FILES],
                                            [f for f in case["target_files"] if f not in HUB_FILES])
                               if set(case["target_files"]) - set(HUB_FILES) else None,
                "symbols": rank_metrics(syms, case["target_symbols"]) if case["target_symbols"] else None,
                "top_files": files[:10], "top_symbols": syms[:10],
            }
    return result


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
    cases = [dict(c, recent_embargo=recent_embargo) for c in cases]
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
    (RUNS_DIR / f"{stamp}-{args.label}.json").write_text(json.dumps({"card": card, "cases": results}, indent=1))
    if not args.no_history:
        with open(HISTORY_FILE, "a") as f:
            f.write(json.dumps(card) + "\n")
    print(json.dumps(card, indent=1))


if __name__ == "__main__":
    main()
