#!/usr/bin/env python3
"""
outputs.py
Brings knowledge_index.db / knowledge_index_private.db in line with the L1 doc store, in place.

The engine's schema is unchanged (fts_documents + vector_documents). One extra table,
l1_doc_hash(doc_id, hash, fts_rowid), records which version of each doc the file holds, so a
sync touches only the rows whose content hash moved: delete the old FTS row by rowid and the
old vector row by doc_id, insert the new ones with vectors from the shared cache. The files
are never unlinked, and a reader never sees a half-built index (one transaction per file).

A DB written by the old full-rebuild indexer has no l1_doc_hash table; its rows are cleared
in place on the first reconcile and re-inserted (vectors come from the cache, not re-embedded).
"""

import sqlite3
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

DDL = """
CREATE VIRTUAL TABLE IF NOT EXISTS fts_documents USING fts5(
    doc_id UNINDEXED, category, title, content, filepath UNINDEXED);
CREATE TABLE IF NOT EXISTS vector_documents (
    doc_id TEXT PRIMARY KEY, category TEXT, title TEXT, snippet TEXT, filepath TEXT, embedding BLOB);
"""


def serialize(vec):
    return struct.pack(f"{len(vec)}f", *vec)


def reconcile(db_path, store, private, cache):
    """Returns (inserted, deleted)."""
    conn = sqlite3.connect(str(db_path), timeout=120)
    conn.executescript(DDL)
    tracked = conn.execute("SELECT 1 FROM sqlite_master WHERE name='l1_doc_hash'").fetchone()

    want = store.doc_hashes(private)
    have = {} if not tracked else {
        d: (h, r) for d, h, r in conn.execute("SELECT doc_id, hash, fts_rowid FROM l1_doc_hash")}
    stale = [d for d, (h, _) in have.items() if want.get(d) != h]
    fresh = [d for d, h in want.items() if have.get(d, (None,))[0] != h]
    if tracked and not stale and not fresh:
        conn.close()
        return 0, 0

    rows = store.docs_by_id(fresh)
    vecs = cache.get_many([(title or "") + " " + (snippet or "") for _, _, title, _, snippet, _, _ in rows])

    with conn:  # one transaction: readers see the old index or the new one, never a mix
        if not tracked:
            conn.execute("DELETE FROM fts_documents")
            conn.execute("DELETE FROM vector_documents")
            conn.execute("CREATE TABLE l1_doc_hash (doc_id TEXT PRIMARY KEY, hash TEXT, fts_rowid INTEGER)")
        for d in stale:
            _, rowid = have[d]
            conn.execute("DELETE FROM fts_documents WHERE rowid=?", (rowid,))
            conn.execute("DELETE FROM vector_documents WHERE doc_id=?", (d,))
            conn.execute("DELETE FROM l1_doc_hash WHERE doc_id=?", (d,))
        for (doc_id, category, title, content, snippet, filepath, h), vec in zip(rows, vecs):
            cur = conn.execute(
                "INSERT INTO fts_documents (doc_id, category, title, content, filepath) VALUES (?,?,?,?,?)",
                (doc_id, category, title, content, filepath))
            conn.execute("INSERT OR REPLACE INTO vector_documents VALUES (?,?,?,?,?,?)",
                         (doc_id, category, title, snippet, filepath, serialize(vec)))
            conn.execute("INSERT OR REPLACE INTO l1_doc_hash VALUES (?,?,?)", (doc_id, h, cur.lastrowid))
    conn.close()
    return len(fresh), len(stale)
