#!/usr/bin/env python3
"""
store.py
L1 doc store (l1/state/l1.db, local only):

  docs        one row per retrievable doc, keyed by a stable doc_id, with a content hash.
              sync_docs() diffs a full snapshot against it: only new/changed rows are
              written, vanished ones deleted. Nothing is ever rebuilt from scratch.
  entities    one integer id per (kind, key): file, symbol, node, skill, commit, session.
              Docs point at their entity; later layers (typed edges) join on it.
  watermarks  per-source progress: last HEAD seen, byte offset per transcript, stat+hash
              per file. JSON values.
  records     per-source raw records (a commit, a session turn, a recovered doc) so the
              JSON corpora can be rewritten from the store without re-mining history.
"""

import hashlib
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import STATE_DIR  # noqa: E402

SCHEMA = """
CREATE TABLE IF NOT EXISTS docs (
    doc_id TEXT PRIMARY KEY, category TEXT, title TEXT, content TEXT, snippet TEXT,
    filepath TEXT, private INTEGER, hash TEXT, entity_id INTEGER);
CREATE TABLE IF NOT EXISTS entities (
    entity_id INTEGER PRIMARY KEY, kind TEXT, key TEXT, UNIQUE(kind, key));
CREATE TABLE IF NOT EXISTS watermarks (source TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS records (
    source TEXT, key TEXT, seq INTEGER, value TEXT, PRIMARY KEY (source, key));
CREATE INDEX IF NOT EXISTS records_order ON records (source, seq);
"""


def doc_hash(row):
    return hashlib.sha256(json.dumps(row, ensure_ascii=False).encode("utf-8", "surrogatepass")).hexdigest()


def entity_for(doc_id, category, title, filepath):
    """(kind, key) naming the thing a doc is about."""
    kind, _, rest = doc_id.partition("::")
    if category == "ast_symbol":
        name = rest
        if name.endswith("Node") and "::" not in name:
            return ("node", name)
        return ("symbol", name)
    if category == "skill":
        return ("skill", rest)
    if category == "git_commit":
        return ("commit", rest)
    if category == "session_history":
        return ("session", rest.rsplit("::", 1)[0])
    if filepath and not filepath.startswith("http"):
        return ("file", filepath)
    return (kind or category, rest or doc_id)


class Store:
    def __init__(self, path=STATE_DIR / "l1.db"):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(path, timeout=120)
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.executescript(SCHEMA)
        self._entities = {}

    # ---- watermarks -------------------------------------------------------------------------
    def get_wm(self, source, default=None):
        row = self.conn.execute("SELECT value FROM watermarks WHERE source=?", (source,)).fetchone()
        return json.loads(row[0]) if row else default

    def set_wm(self, source, value):
        self.conn.execute("INSERT OR REPLACE INTO watermarks VALUES (?,?)", (source, json.dumps(value)))
        self.conn.commit()

    def wm_prefix(self, prefix):
        return {s: json.loads(v) for s, v in
                self.conn.execute("SELECT source, value FROM watermarks WHERE source LIKE ?", (prefix + "%",))}

    # ---- records ----------------------------------------------------------------------------
    def put_records(self, source, items, remove=()):
        """items: [(key, seq, value)]. Returns True if anything changed."""
        changed = False
        for key in remove:
            changed |= self.conn.execute("DELETE FROM records WHERE source=? AND key=?", (source, key)).rowcount > 0
        for key, seq, value in items:
            blob = json.dumps(value, ensure_ascii=False)
            row = self.conn.execute("SELECT seq, value FROM records WHERE source=? AND key=?", (source, key)).fetchone()
            if row and row[0] == seq and row[1] == blob:
                continue
            self.conn.execute("INSERT OR REPLACE INTO records VALUES (?,?,?,?)", (source, key, seq, blob))
            changed = True
        self.conn.commit()
        return changed

    def record_keys(self, source):
        return {k for (k,) in self.conn.execute("SELECT key FROM records WHERE source=?", (source,))}

    def records(self, source):
        return [json.loads(v) for (v,) in
                self.conn.execute("SELECT value FROM records WHERE source=? ORDER BY seq, key", (source,))]

    def reorder(self, source, keys_in_order):
        self.conn.executemany("UPDATE records SET seq=? WHERE source=? AND key=?",
                              [(i, source, k) for i, k in enumerate(keys_in_order)])
        self.conn.commit()

    # ---- entities ---------------------------------------------------------------------------
    def entity_id(self, kind, key):
        k = (kind, key)
        eid = self._entities.get(k)
        if eid is None:
            self.conn.execute("INSERT OR IGNORE INTO entities (kind, key) VALUES (?,?)", k)
            eid = self.conn.execute("SELECT entity_id FROM entities WHERE kind=? AND key=?", k).fetchone()[0]
            self._entities[k] = eid
        return eid

    # ---- docs -------------------------------------------------------------------------------
    def sync_docs(self, rows, private_categories):
        """rows: full snapshot of (doc_id, category, title, content, snippet, filepath).
        Upserts changed rows, deletes vanished ones. Returns (added, changed, deleted)."""
        have = dict(self.conn.execute("SELECT doc_id, hash FROM docs"))
        seen = set()
        added = changed = 0
        for row in rows:
            doc_id, category, title, content, snippet, filepath = row
            if doc_id in seen:
                continue  # duplicate id: keep the first
            seen.add(doc_id)
            h = doc_hash(row)
            old = have.get(doc_id)
            if old == h:
                continue
            kind, key = entity_for(doc_id, category, title, filepath)
            eid = self.entity_id(kind, key)
            if filepath and category == "ast_symbol":
                self.entity_id("file", filepath)
            self.conn.execute("INSERT OR REPLACE INTO docs VALUES (?,?,?,?,?,?,?,?,?)",
                              (doc_id, category, title, content, snippet, filepath,
                               int(category in private_categories), h, eid))
            if old is None:
                added += 1
            else:
                changed += 1
        gone = [d for d in have if d not in seen]
        self.conn.executemany("DELETE FROM docs WHERE doc_id=?", [(d,) for d in gone])
        self.conn.commit()
        return added, changed, len(gone)

    def doc_hashes(self, private):
        return dict(self.conn.execute("SELECT doc_id, hash FROM docs WHERE private=?", (int(private),)))

    def docs_by_id(self, ids):
        out = []
        ids = list(ids)
        for i in range(0, len(ids), 500):
            chunk = ids[i:i + 500]
            q = ("SELECT doc_id, category, title, content, snippet, filepath, hash FROM docs WHERE doc_id IN (%s)"
                 % ",".join("?" * len(chunk)))
            out.extend(self.conn.execute(q, chunk))
        return out
