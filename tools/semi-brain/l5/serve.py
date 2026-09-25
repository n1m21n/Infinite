#!/usr/bin/env python3
"""
serve.py
The warm brief server: runs as a thread inside the watch daemon (l1/brain_watchd.py) so the
prompt hook gets a brief in tens of milliseconds instead of paying a cold engine start (~0.7 s)
on every prompt.

  socket   $GITDIR/brain_brief.sock (next to the daemon's pidfile, so worktrees find it)
  request  one JSON line {"query": str, "session": str, "json": bool, "hook": bool}
  reply    one JSON line {"brief": str, "data": {...}, "ms": float, "arm": str}
  arms     for the prompt hook only (hook=true), so 5_evals/live.py can measure what a brief
           saves: "holdout" (HOLDOUT of prompts, at random) gets no brief at all - the baseline;
           "quiet" gets none either, because l5/gate.py judged the brain unsure (a brief that
           points nowhere costs tokens and saves none); "shown" gets it. The CLI and the MCP
           tool always get it ("ask"). The brief is built and logged either way, so a held-out
           or quiet turn still says what it would have pointed at
  engine   built once, then rebuilt in a background thread (reusing the loaded embedding model)
           when a sync has finished since it was built - l1/state/last_sync.json is written at
           the end of every sync - and swapped in when ready, so no request waits for a reload
  log      every answered query goes to l1/state/briefs.jsonl (local only), which the outcome
           log joins with what was actually read and edited afterwards; the L0 note ids it
           showed are what the sleep job credits (l1/sleep.py)
"""

import json
import os
import random
import socket
import threading
import time
from pathlib import Path

MAX_QUERY = 1500
MAX_REQUEST = 64 * 1024
HOLDOUT = 0.25


def pick_arm(req, confidence, rng=random.random):
    from l5.gate import confident
    if not req.get("hook"):
        return "ask"
    if rng() < HOLDOUT:
        return "holdout"
    return "shown" if confident(confidence) else "quiet"


class BriefServer(threading.Thread):
    def __init__(self, sock_path, state_dir, log=print):
        super().__init__(daemon=True, name="brief-server")
        self.sock_path = Path(sock_path)
        self.state_dir = Path(state_dir)
        self.log = log
        self._engine = None
        self._stamp = None

    def _sync_stamp(self):
        try:
            return (self.state_dir / "last_sync.json").stat().st_mtime
        except OSError:
            return None

    RELOAD_CHECK_S = 2.0

    def _load(self):
        from brain_core import SemiBrainCognitiveEngine
        t = time.perf_counter()
        stamp = self._sync_stamp()
        old = self._engine
        eng = SemiBrainCognitiveEngine()
        if old is not None and old.retriever._embed_model is not None:
            eng.retriever._embed_model = old.retriever._embed_model
        eng.retriever._load_vector_cache()
        eng._areas()
        eng.analyze_problem("warm up the brief engine caches")  # sqlite pages, onnx, symbol words
        self._engine, self._stamp = eng, stamp
        self.log(f"brief engine {'re' if old else ''}loaded in {time.perf_counter() - t:.2f}s")

    def engine(self):
        if self._engine is None:
            self._load()
        return self._engine

    def _reloader(self):
        while True:
            time.sleep(self.RELOAD_CHECK_S)
            if self._engine is not None and self._sync_stamp() != self._stamp:
                try:
                    self._load()
                except Exception as e:
                    self.log(f"brief engine reload failed: {e!r}")

    def answer(self, req):
        from l5.brief import brief_data, render
        t = time.perf_counter()
        query = (req.get("query") or "")[:MAX_QUERY]
        engine = self.engine()
        frame = engine.analyze_problem(query, session=req.get("session", ""))
        data = brief_data(engine, frame)
        ms = round((time.perf_counter() - t) * 1000, 1)
        main_regions = next((f["region_ids"] for f in data["files"] if f.get("region_ids")), [])
        text = render(data)
        arm = pick_arm(req, frame.confidence)
        rec = {"t": time.time(), "session": req.get("session", ""), "query": query,
               "arm": arm, "chars": len(text), "confidence": frame.confidence,
               "files": [f["file"] for f in data["files"]],
               "symbols": [s["symbol"] for s in data["symbols"]],
               "notes": [n["id"] for n in data.get("notes", [])], "ms": ms,
               "main_regions": main_regions}
        try:
            with open(self.state_dir / "briefs.jsonl", "a") as f:
                f.write(json.dumps(rec) + "\n")
        except OSError:
            pass
        return {"brief": text if arm in ("shown", "ask") else "",
                "data": data if req.get("json") else None, "ms": ms, "arm": arm}

    def run(self):
        try:
            self.sock_path.unlink()
        except OSError:
            pass
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(str(self.sock_path))
        os.chmod(self.sock_path, 0o600)
        srv.listen(8)
        try:
            self.engine()
        except Exception as e:  # keep serving; the next request retries the load
            self.log(f"brief engine load failed: {e!r}")
        threading.Thread(target=self._reloader, daemon=True, name="brief-reloader").start()
        while True:
            conn, _ = srv.accept()
            with conn:
                try:
                    conn.settimeout(2.0)
                    buf = b""
                    while not buf.endswith(b"\n") and len(buf) < MAX_REQUEST:
                        chunk = conn.recv(8192)
                        if not chunk:
                            break
                        buf += chunk
                    reply = self.answer(json.loads(buf or b"{}"))
                except Exception as e:
                    self.log(f"brief request failed: {e!r}")
                    reply = {"brief": "", "error": repr(e)}
                try:
                    conn.sendall(json.dumps(reply).encode() + b"\n")
                except OSError:
                    pass


def ask(sock_path, query, session="", as_json=False, timeout=3.0, hook=False):
    """Client side, stdlib only (the hook imports this file without the engine)."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(str(sock_path))
        s.sendall(json.dumps({"query": query, "session": session, "json": as_json, "hook": hook}).encode() + b"\n")
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
        return json.loads(buf)
    finally:
        s.close()
