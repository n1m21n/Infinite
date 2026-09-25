#!/usr/bin/env python3
"""
sync.py
One incremental semi-brain sync, in-process. Replaces the ten back-to-back extractor
subprocesses that each re-read the whole history.

  sources   commits, docs, skills, byox, claude, antigravity   (l1/sources.py)
            ast graph, re-parsing only changed src/ files          (l1/ast_source.py)
  derived   session analysis   when a session corpus changed, at most every DERIVED_GAP_S
            dev trajectory     when sessions or the AST graph changed, at most every DERIVED_GAP_S
            training data      when commits/docs/skills/byox changed
            The two refits are global (k-means topics, trend summaries): one new chat turn barely
            moves them, and under background QoS each costs tens of seconds. Between refits they
            stay owed; new turns still reach the index as docs on every sync. --full forces them.
  index     corpora -> L1 doc store (content-hash diff) -> both index DBs reconciled in place

Per-stage timings go to stdout and l1/state/last_sync.json.
"""

import json
import sys
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import STATE_DIR, SEMI_BRAIN_DIR  # noqa: E402
from l1 import sources  # noqa: E402
from l1.store import Store  # noqa: E402
from l1.ast_source import sync_ast  # noqa: E402

sys.path.insert(0, str(SEMI_BRAIN_DIR / "3_datasets"))


class Run:
    def __init__(self):
        self.timings = {}
        self.changed = {}
        self.errors = {}

    def stage(self, name, fn, *args):
        t0 = time.perf_counter()
        try:
            result = fn(*args)
        except Exception:
            # One broken source must not stop the others; the next sync retries it.
            self.errors[name] = traceback.format_exc(limit=3)
            print(f"  ! {name} failed:\n{self.errors[name]}", flush=True)
            result = False
        dt = time.perf_counter() - t0
        self.timings[name] = round(dt, 2)
        self.changed[name] = bool(result) if not isinstance(result, dict) else result
        print(f"  {name:<18} {dt:7.2f}s  {'changed' if result else '-'}", flush=True)
        return result


def run_analysis(cache):
    import analyze_session_linguistics
    analyze_session_linguistics.analyze(cache)
    return True


def run_trajectory(cache):
    import classify_dev_trajectory
    classify_dev_trajectory.main()
    return True


def run_training(cache):
    import compile_training_data
    compile_training_data.main()
    return True


def run_index(store, cache):
    import index_codebase as ic
    from l1.outputs import reconcile
    docs = ic.prepare_documents(ic.load_corpora())
    added, changed, deleted = store.sync_docs(docs, ic.PRIVATE_CATEGORIES)
    pub = reconcile(ic.DB_FILE, store, False, cache)
    priv = reconcile(ic.PRIVATE_DB_FILE, store, True, cache)
    return {"docs": len(docs), "added": added, "changed": changed, "deleted": deleted,
            "public_ins_del": pub, "private_ins_del": priv, "embedded": cache.misses}


DERIVED = {"session_analysis": run_analysis, "dev_trajectory": run_trajectory,
           "training_data": run_training}
DERIVED_GAP_S = {"session_analysis": 1800, "dev_trajectory": 1800}


def sync(full=False):
    from l1.vector_cache import VectorCache

    t0 = time.perf_counter()
    store = Store()
    cache = VectorCache()
    run = Run()

    print("semi-brain L1 sync", flush=True)
    commits = run.stage("commits", sources.sync_commits, store)
    docs = run.stage("docs", sources.sync_docs, store)
    skills = run.stage("skills", sources.sync_skills, store)
    byox = run.stage("byox", sources.sync_byox, store)
    claude = run.stage("claude", sources.sync_claude_sessions, store)
    antigravity = run.stage("antigravity", sources.sync_antigravity, store)
    ast = run.stage("ast", sync_ast, store)

    # Derived stages owed a run survive a failed or interrupted sync (first sync: all of them).
    owed = set(store.get_wm("sync:owed", list(DERIVED)))
    if claude or antigravity:
        owed |= {"session_analysis", "dev_trajectory"}
    if ast:
        owed.add("dev_trajectory")
    if commits or docs or skills or byox:
        owed.add("training_data")
    store.set_wm("sync:owed", sorted(owed))
    now = time.time()
    deferred = {}
    for name, fn in DERIVED.items():
        if name not in owed:
            continue
        due = store.get_wm("derived:last:" + name, 0) + DERIVED_GAP_S.get(name, 0)
        if not full and due > now:
            deferred[name] = round(due)
            continue
        run.stage(name, _quiet(fn), cache)
        if name not in run.errors:
            owed.discard(name)
            store.set_wm("sync:owed", sorted(owed))
            store.set_wm("derived:last:" + name, now)
    stats = run.stage("index", run_index, store, cache)

    total = round(time.perf_counter() - t0, 2)
    report = {"when": time.strftime("%Y-%m-%dT%H:%M:%S"), "total_s": total, "timings": run.timings,
              "changed": run.changed, "errors": list(run.errors), "index": stats,
              "deferred_until": deferred}
    (STATE_DIR / "last_sync.json").write_text(json.dumps(report, indent=1))
    if deferred:
        print(f"  deferred           {', '.join(sorted(deferred))} (due {time.strftime('%H:%M', time.localtime(min(deferred.values())))})", flush=True)
    print(f"  {'total':<18} {total:7.2f}s  index={stats}", flush=True)
    return report


def _quiet(fn):
    """The legacy scripts print progress per k-means iteration etc.; keep the sync log short."""
    def wrapped(*a):
        import contextlib
        import io
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                return fn(*a)
        except Exception:
            sys.stdout.write(buf.getvalue()[-2000:])
            raise
    return wrapped


if __name__ == "__main__":
    sync(full="--full" in sys.argv)
