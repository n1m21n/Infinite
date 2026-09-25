#!/usr/bin/env python3
"""
ast_cache.py
Content-keyed tree-sitter results: (path, git blob sha) -> build_ast_graph.extract_from_bytes
output, in l1/state/ast_blob_cache.db. The sync re-parses only files whose content changed; the
replay benchmark parses each historical file version once across all its parent trees.
"""

import hashlib
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "1_extractors"))
from l1 import STATE_DIR  # noqa: E402

CODE_EXTS = (".cpp", ".h", ".mm")


def blob_sha(data):
    """The id git gives these bytes, so working-tree files and historical blobs share keys."""
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


class BytesReader:
    def __init__(self, data):
        self.data = data

    def read(self, blob):
        return self.data


def graph_order(items):
    """(path, ...) pairs in the order the graph is assembled: .cpp, then .h, then .mm, by path."""
    return sorted(items, key=lambda pb: (CODE_EXTS.index(Path(pb[0]).suffix), pb[0]))


class ParseCache:
    """(path, blob sha) -> build_ast_graph.extract_from_bytes result, persisted in l1/state."""

    def __init__(self, db_path=STATE_DIR / "ast_blob_cache.db"):
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(db_path, timeout=60)
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("CREATE TABLE IF NOT EXISTS parsed (path TEXT, blob TEXT, result TEXT, PRIMARY KEY (path, blob))")
        self.mem = {}
        self._extract = None

    def has(self, path, blob):
        return self.conn.execute("SELECT 1 FROM parsed WHERE path=? AND blob=?", (path, blob)).fetchone() is not None

    def get(self, path, blob, reader):
        """reader: anything with .read(blob) -> bytes (a BlobReader, or BytesReader)."""
        k = (path, blob)
        if k in self.mem:
            return self.mem[k]
        row = self.conn.execute("SELECT result FROM parsed WHERE path=? AND blob=?", k).fetchone()
        if row:
            res = json.loads(row[0])
        else:
            if self._extract is None:
                from build_ast_graph import extract_from_bytes
                self._extract = extract_from_bytes
            data = reader.read(blob)
            res = self._extract(path, data or b"")
            self.conn.execute("INSERT OR REPLACE INTO parsed VALUES (?,?,?)", (path, blob, json.dumps(res)))
            self.conn.commit()
        self.mem[k] = res
        return res
