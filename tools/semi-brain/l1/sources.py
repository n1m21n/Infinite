#!/usr/bin/env python3
"""
sources.py
Incremental versions of the extractors. Each source keeps its raw records in the L1 store,
advances a watermark, and rewrites its legacy JSON corpus (which the engine, the dataset
compiler and the analysis scripts still read) only when a record actually changed.

  commits      records keyed by full hash; `git rev-list HEAD` decides what is new or gone
  docs         deleted docs recovered only for commits since the last HEAD seen
  skills       re-read every time (about 60 small files)
  byox         the network fetch runs at most once per BYOX_TTL
  claude       per-transcript byte offset; only lines appended since the last sync are parsed
  antigravity  same, plus a per-transcript "mentions this repo" flag

Each function returns True when its corpus changed.
"""

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import REPO_PATH, SEMI_BRAIN_DIR  # noqa: E402

EXTRACTORS = SEMI_BRAIN_DIR / "1_extractors"
OUT = EXTRACTORS / "output"
sys.path.insert(0, str(EXTRACTORS))

BYOX_TTL = 7 * 24 * 3600


def git(*args):
    return subprocess.run(["git", *args], cwd=REPO_PATH, capture_output=True, text=True).stdout.strip()


def write_if_changed(path, obj):
    """json.dump(obj, indent=2) into path unless the file already holds exactly that."""
    data = json.dumps(obj, indent=2).encode("utf-8", "surrogatepass")
    path = Path(path)
    if path.exists() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)
    return True


def file_digest(path):
    path = Path(path)
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


# ---- git commits --------------------------------------------------------------------------------
def sync_commits(store):
    from mine_git_history import commit_files, mine_commits
    order = git("rev-list", "HEAD").split()
    have = store.record_keys("commit")
    new = [h for h in order if h not in have]
    gone = have - set(order)
    for i in range(0, len(new), 200):
        mined = mine_commits(new[i:i + 200])
        files = commit_files([c["hash"] for c in mined])
        store.put_records("commit", [(c["hash"], 0, dict(c, files=files.get(c["hash"], []))) for c in mined])
    # Records mined before commits carried their file list (L3's commit -> file edges): backfill.
    missing = [c for c in store.records("commit") if "files" not in c]
    for i in range(0, len(missing), 500):
        chunk = missing[i:i + 500]
        files = commit_files([c["hash"] for c in chunk])
        store.put_records("commit", [(c["hash"], 0, dict(c, files=files.get(c["hash"], []))) for c in chunk])
    if gone:
        store.put_records("commit", [], remove=gone)
    by_hash = {c["hash"]: c for c in store.records("commit")}
    return write_if_changed(OUT / "git_commits_corpus.json", [by_hash[h] for h in order if h in by_hash])


# ---- recovered docs -----------------------------------------------------------------------------
def sync_docs(store):
    from recover_git_docs import deleted_paths, recover_path
    head = git("rev-parse", "HEAD")
    last = store.get_wm("docs:head")
    if last == head and (OUT / "recovered_docs_corpus.json").exists():
        return False
    incremental = last and subprocess.run(["git", "merge-base", "--is-ancestor", last, head],
                                          cwd=REPO_PATH).returncode == 0
    paths = deleted_paths(f"{last}..{head}") if incremental else deleted_paths()
    items = []
    for p in paths:
        rec = recover_path(p)
        if rec:
            items.append((p, 0, rec))
    if not incremental:
        stale = store.record_keys("doc") - {k for k, _, _ in items}
        store.put_records("doc", items, remove=stale)
    else:
        store.put_records("doc", items)
    store.set_wm("docs:head", head)
    return write_if_changed(OUT / "recovered_docs_corpus.json", store.records("doc"))


# ---- skills -------------------------------------------------------------------------------------
def sync_skills(store):
    from parse_skills_and_rules import collect_records
    return write_if_changed(OUT / "skills_and_invariants_corpus.json", collect_records())


# ---- build-your-own-x ---------------------------------------------------------------------------
def sync_byox(store):
    corpus = OUT / "build_your_own_x_corpus.json"
    fetched = store.get_wm("byox:fetched_at", 0)
    if corpus.exists() and time.time() - fetched < BYOX_TTL:
        return False
    from ingest_build_your_own_x import fetch_and_parse_byox
    before = file_digest(corpus)
    try:
        fetch_and_parse_byox()
    except Exception as e:  # offline: keep the last corpus, try again next sync
        print(f"  byox fetch failed ({e}); keeping the existing corpus")
        return False
    store.set_wm("byox:fetched_at", time.time())
    return file_digest(corpus) != before


# ---- transcripts --------------------------------------------------------------------------------
class TurnParser:
    """Resumable form of mine_session / mine_conversation: feed it lines, it emits finished
    turns. `state` (JSON) carries the half-built turn between syncs."""

    def __init__(self, kind, key_prefix, state=None):
        import mine_session_history as ms
        import mine_antigravity_history as ma
        self.kind = kind
        self.ms, self.ma = ms, ma
        self.prefix = key_prefix
        s = state or {}
        self.n = s.get("n", 0)
        self.pending = s.get("pending")  # {"user": str, "meta": {...}, "parts": [str]}
        self.out = []

    def state(self):
        return {"n": self.n, "pending": self.pending}

    def _turn(self, pending):
        user_text = pending["user"].strip()
        assistant_text = "\n".join(p for p in pending["parts"] if p.strip()).strip()
        if len(user_text) < (self.ms if self.kind == "claude" else self.ma).MIN_USER_CHARS:
            return None
        meta = pending["meta"]
        if self.kind == "claude":
            if user_text.startswith(self.ms.BOILERPLATE_PREFIXES):
                return None
            return {"session_id": meta["session_id"], "timestamp": meta.get("timestamp", ""),
                    "cwd": meta.get("cwd", ""), "git_branch": meta.get("gitBranch", ""),
                    "user_text": user_text[:self.ms.MAX_USER_CHARS],
                    "assistant_text": assistant_text[:self.ms.MAX_ASSISTANT_CHARS]}
        return {"session_id": meta["session_id"], "timestamp": meta.get("timestamp", ""),
                "cwd": self.ma.PROJECT_PATH_MARKER, "git_branch": "", "source_tool": "antigravity",
                "user_text": user_text[:self.ma.MAX_USER_CHARS],
                "assistant_text": assistant_text[:self.ma.MAX_ASSISTANT_CHARS]}

    def _flush(self):
        if self.pending is None:
            return
        turn = self._turn(self.pending)
        if turn is not None:
            self.out.append((f"{self.prefix}::{self.n:06d}", turn))
            self.n += 1

    def _add_part(self, text):
        parts = self.pending["parts"]
        # Only the first MAX_ASSISTANT_CHARS survive, so stop carrying text past that.
        if len("\n".join(p for p in parts if p.strip()).strip()) <= self.ms.MAX_ASSISTANT_CHARS:
            parts.append(text)

    def feed(self, line, session_id):
        line = line.strip()
        if not line:
            return
        try:
            evt = json.loads(line)
        except json.JSONDecodeError:
            return
        if self.kind == "claude":
            if evt.get("isSidechain"):
                return
            etype = evt.get("type")
            if etype == "user":
                text = self.ms.extract_user_text(evt.get("message", {}))
                if text is None:
                    return
                self._flush()
                self.pending = {"user": text, "parts": [],
                                "meta": {"session_id": session_id, "timestamp": evt.get("timestamp", ""),
                                         "cwd": evt.get("cwd", ""), "gitBranch": evt.get("gitBranch", "")}}
            elif etype == "assistant" and self.pending is not None:
                self._add_part(self.ms.extract_assistant_text(evt.get("message", {})))
        else:
            etype = evt.get("type")
            if etype == "USER_INPUT":
                text = self.ma.extract_user_text(evt.get("content", ""))
                if text is None:
                    return
                self._flush()
                self.pending = {"user": text, "parts": [],
                                "meta": {"session_id": session_id, "timestamp": evt.get("created_at", "")}}
            elif etype == "PLANNER_RESPONSE" and self.pending is not None:
                content = evt.get("content", "")
                if content and content.strip():
                    self._add_part(content.strip())

    def provisional(self):
        """The in-progress turn as it would be flushed now (a live session's latest ask is
        searchable before the next one arrives); same key it will be finalised under."""
        if self.pending is None:
            return None
        turn = self._turn(self.pending)
        return (f"{self.prefix}::{self.n:06d}", turn) if turn else None


def _read_new(path, offset):
    """Complete lines appended after `offset`, and the new offset."""
    with open(path, "rb") as f:
        f.seek(offset)
        data = f.read()
    end = data.rfind(b"\n")
    lines = data[:end + 1].decode("utf-8", "replace").splitlines() if end >= 0 else []
    tail = data[end + 1:]
    if tail.strip():
        # A last line without a newline is either still being written (not valid JSON yet)
        # or the finished end of a file that never got one (take it).
        try:
            json.loads(tail)
            lines.append(tail.decode("utf-8", "replace"))
            end = len(data) - 1
        except ValueError:
            pass
    return lines, offset + end + 1


def _sync_transcripts(store, source, files, kind, corpus_name, relevance_marker=None):
    """files: {key_prefix: path}. Records are kept for transcripts that later disappear
    (Claude Code prunes old ones); the store is the long-term memory."""
    wms = store.wm_prefix(source + ":")
    changed = False
    for prefix, path in files.items():
        wm_key = f"{source}:{prefix}"
        st = path.stat()
        wm = wms.get(wm_key)
        if wm and wm["size"] == st.st_size and wm["mtime"] == st.st_mtime and wm.get("ino") == st.st_ino:
            continue
        restart = (not wm or st.st_size < wm["offset"] or wm.get("ino") != st.st_ino)
        offset = 0 if restart else wm["offset"]
        relevant = True if relevance_marker is None else (False if restart else wm.get("relevant", False))
        parser = TurnParser(kind, prefix, None if restart else wm.get("parser"))
        lines, new_offset = _read_new(path, offset)

        if relevance_marker is not None and not relevant:
            if any(relevance_marker in l for l in lines):
                relevant = True
                if offset:  # became relevant mid-file: everything before counts too
                    parser = TurnParser(kind, prefix)
                    lines, new_offset = _read_new(path, 0)
        if restart and not relevant:
            old = {k for k in store.record_keys(source) if k.startswith(prefix + "::")}
            changed |= store.put_records(source, [], remove=old)
        if relevant:
            for line in lines:
                parser.feed(line, prefix)
            prov = parser.provisional()
            items = [(k, 0, t) for k, t in parser.out + ([prov] if prov else [])]
            if restart:
                old = {k for k in store.record_keys(source) if k.startswith(prefix + "::")}
                changed |= store.put_records(source, items, remove=old - {k for k, _, _ in items})
            else:
                changed |= store.put_records(source, items)
        store.set_wm(wm_key, {"offset": new_offset, "size": st.st_size, "mtime": st.st_mtime,
                              "ino": st.st_ino, "relevant": relevant, "parser": parser.state()})
    corpus = OUT / corpus_name
    if changed or not corpus.exists():
        return write_if_changed(corpus, store.records(source))
    return False


def sync_claude_sessions(store):
    from mine_session_history import SESSIONS_DIR
    files = {p.stem: p for p in sorted(SESSIONS_DIR.glob("*.jsonl"))} if SESSIONS_DIR.exists() else {}
    return _sync_transcripts(store, "claude", files, "claude", "session_history_corpus.json")


def sync_antigravity(store):
    from mine_antigravity_history import BRAIN_DIR, PROJECT_PATH_MARKER
    files = {}
    if BRAIN_DIR.exists():
        for d in sorted(p for p in BRAIN_DIR.iterdir() if p.is_dir()):
            t = d / ".system_generated" / "logs" / "transcript.jsonl"
            if t.exists():
                files[d.name] = t
    return _sync_transcripts(store, "antigravity", files, "antigravity", "antigravity_history_corpus.json",
                             relevance_marker=PROJECT_PATH_MARKER)
