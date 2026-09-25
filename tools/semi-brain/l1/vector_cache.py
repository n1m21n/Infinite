#!/usr/bin/env python3
"""
vector_cache.py
One embedding cache shared by every stage (index, session analysis, replay benchmark).
Key = sha256(model + NUL + text), value = float32 vector. A text is embedded once, ever:
a new commit costs one embedding for its new docs, not a re-embed of the whole corpus.

Local-only: vectors of private session text are as private as the text itself.
"""

import hashlib
import sqlite3
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import STATE_DIR, EMBED_MODEL  # noqa: E402

DEFAULT_DB = STATE_DIR / "vectors.db"
BATCH = 64


def text_key(model, text):
    return hashlib.sha256((model + "\0" + text).encode("utf-8", "surrogatepass")).digest()


class VectorCache:
    def __init__(self, db_path=DEFAULT_DB, model=EMBED_MODEL):
        self.model = model
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(self.db_path, timeout=60)
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("CREATE TABLE IF NOT EXISTS vectors (key BLOB PRIMARY KEY, model TEXT, vec BLOB)")
        self._embedder = None
        self.hits = 0
        self.misses = 0

    @property
    def embedder(self):
        if self._embedder is None:
            from fastembed import TextEmbedding
            # fastembed defaults to tempfile.gettempdir()/fastembed_cache, which macOS/CI can
            # sweep between runs, forcing a re-download; STATE_DIR is this machine's stable
            # local state, already gitignored and never pushed (see SKILL.md privacy section).
            self._embedder = TextEmbedding(model_name=self.model,
                                           cache_dir=str(STATE_DIR / "fastembed_cache"))
        return self._embedder

    def _lookup(self, keys):
        found = {}
        for i in range(0, len(keys), 500):
            chunk = keys[i:i + 500]
            q = "SELECT key, vec FROM vectors WHERE key IN (%s)" % ",".join("?" * len(chunk))
            for k, v in self.conn.execute(q, chunk):
                found[bytes(k)] = np.frombuffer(v, dtype=np.float32)
        return found

    def get_many(self, texts):
        """Vectors for texts, in order; embeds and stores only the ones never seen."""
        keys = [text_key(self.model, t) for t in texts]
        found = self._lookup(list(set(keys)))
        missing = {}
        for k, t in zip(keys, texts):
            if k not in found and k not in missing:
                missing[k] = t
        self.hits += len(texts) - len(missing)
        self.misses += len(missing)
        if missing:
            mk = list(missing.keys())
            mt = [missing[k] for k in mk]
            for i in range(0, len(mt), BATCH):
                vecs = list(self.embedder.embed(mt[i:i + BATCH], batch_size=BATCH))
                rows = []
                for k, v in zip(mk[i:i + BATCH], vecs):
                    v = np.asarray(v, dtype=np.float32)
                    found[k] = v
                    rows.append((k, self.model, v.tobytes()))
                self.conn.executemany("INSERT OR REPLACE INTO vectors VALUES (?,?,?)", rows)
                self.conn.commit()
        return [found[k] for k in keys]

    # fastembed-compatible surface, so existing code that calls model.embed(texts) can take a cache
    def embed(self, texts, batch_size=None):
        return self.get_many(list(texts))

    def seed_from_index(self, db_path):
        """Import vectors already computed by the old full-rebuild indexer (text = title + ' ' +
        snippet, the exact string index_codebase.py embeds). Returns rows imported."""
        src = sqlite3.connect(str(db_path))
        rows = []
        for title, snippet, blob in src.execute("SELECT title, snippet, embedding FROM vector_documents"):
            rows.append((text_key(self.model, (title or "") + " " + (snippet or "")), self.model, bytes(blob)))
        src.close()
        before = self.count()
        self.conn.executemany("INSERT OR IGNORE INTO vectors VALUES (?,?,?)", rows)
        self.conn.commit()
        return self.count() - before

    def count(self):
        return self.conn.execute("SELECT COUNT(*) FROM vectors").fetchone()[0]


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", nargs="*", default=[], help="index DBs to import vectors from")
    args = ap.parse_args()
    vc = VectorCache()
    for db in args.seed:
        print(f"seeded {vc.seed_from_index(db)} vectors from {db}")
    print(f"{vc.count()} vectors in {vc.db_path}")
