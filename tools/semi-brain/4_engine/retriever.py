#!/usr/bin/env python3
"""
retriever.py
Hybrid Retriever combining BM25 (SQLite FTS5) + Dense Cosine Similarity (FastEmbed)
using Reciprocal Rank Fusion (RRF).
"""

import sqlite3
import numpy as np
from pathlib import Path
from typing import List, Dict, Any
from fastembed import TextEmbedding

SEMI_BRAIN_DIR = Path(__file__).resolve().parents[1]
DB_FILE = SEMI_BRAIN_DIR / "1_extractors" / "output" / "knowledge_index.db"
# Local-only index of chat/session-derived documents (see index_codebase.py). Absent on a fresh
# clone - the retriever then answers from the public index alone.
PRIVATE_DB_FILE = SEMI_BRAIN_DIR / "1_extractors" / "output" / "knowledge_index_private.db"

class HybridRetriever:
    def __init__(self):
        self.db_paths = [DB_FILE, PRIVATE_DB_FILE]
        self._embed_model = None
        
    @property
    def embed_model(self):
        if self._embed_model is None:
            self._embed_model = TextEmbedding(model_name="BAAI/bge-small-en-v1.5")
        return self._embed_model

    def unpack_vector(self, blob: bytes) -> np.ndarray:
        return np.frombuffer(blob, dtype=np.float32)

    def bm25_search(self, query: str, limit: int = 15) -> List[Dict[str, Any]]:
        """Run BM25 search against SQLite FTS5."""
        # Clean query for FTS5 (escape quotes and special operators)
        clean_q = " OR ".join([f'"{w}"' for w in query.replace('"', '').split() if len(w) > 2])
        if not clean_q:
            clean_q = f'"{query}"'

        sql = """
            SELECT doc_id, category, title, content, filepath, rank
            FROM fts_documents
            WHERE fts_documents MATCH ?
            ORDER BY rank
            LIMIT ?
        """
        results = []
        for db_path in self.db_paths:
            if not db_path.exists():
                continue
            conn = sqlite3.connect(db_path)
            try:
                for row in conn.execute(sql, (clean_q, limit)):
                    results.append({
                        "doc_id": row[0],
                        "category": row[1],
                        "title": row[2],
                        "snippet": row[3][:300],
                        "filepath": row[4],
                        "bm25_rank": row[5]
                    })
            except sqlite3.OperationalError:
                pass
            finally:
                conn.close()

        # FTS5 rank is negative BM25 (lower = better), comparable across DBs built the same way.
        results.sort(key=lambda r: r["bm25_rank"])
        return results[:limit]

    def _load_vector_cache(self):
        """Two tiers, no float32 matrix kept:
          fast     1 sign bit per dimension (48 bytes/doc), Hamming distance -> RESCORE candidates
          precise  int8 per-vector-scaled codes, dot product with the float query re-ranks them
        """
        if getattr(self, "_vector_cache", None) is not None:
            return
        rows = []
        for db_path in self.db_paths:
            if not db_path.exists():
                continue
            conn = sqlite3.connect(db_path)
            rows.extend(conn.execute(
                "SELECT doc_id, category, title, snippet, filepath, embedding FROM vector_documents").fetchall())
            conn.close()

        self._doc_meta = [{"doc_id": r[0], "category": r[1], "title": r[2], "snippet": r[3], "filepath": r[4]}
                          for r in rows]
        if not rows:
            self._vector_cache = False
            return
        m = np.frombuffer(b"".join(r[5] for r in rows), dtype=np.float32).reshape(len(rows), -1)
        m = m / np.maximum(np.linalg.norm(m, axis=1, keepdims=True), 1e-9)
        self._bits = np.packbits(m > 0, axis=1)
        scale = np.maximum(np.abs(m).max(axis=1), 1e-9) / 127.0
        self._int8 = np.round(m / scale[:, None]).astype(np.int8)
        self._scale = scale.astype(np.float32)
        self._vector_cache = True

    RESCORE = 1000
    _POPCOUNT = np.array([bin(i).count("1") for i in range(256)], dtype=np.uint16)

    def dense_search(self, query: str, limit: int = 15) -> List[Dict[str, Any]]:
        """Hamming top-RESCORE on sign bits, then int8 cosine re-rank of those candidates."""
        self._load_vector_cache()
        if not getattr(self, "_vector_cache", False):
            return []

        query_vec = np.asarray(list(self.embed_model.embed([query]))[0], dtype=np.float32)
        query_vec = query_vec / np.maximum(np.linalg.norm(query_vec), 1e-9)

        n = len(self._doc_meta)
        qbits = np.packbits(query_vec > 0)
        ham = self._POPCOUNT[np.bitwise_xor(self._bits, qbits)].sum(axis=1)
        c = min(self.RESCORE, n)
        cand = np.argpartition(ham, c - 1)[:c] if c < n else np.arange(n)
        sims = (self._int8[cand].astype(np.float32) @ query_vec) * self._scale[cand]
        k = min(limit, len(cand))
        top = np.argpartition(-sims, k - 1)[:k]
        top = top[np.argsort(-sims[top])]

        scored = []
        for j in top:
            item = dict(self._doc_meta[cand[j]])
            item["dense_score"] = float(sims[j])
            scored.append(item)
        return scored

    def hybrid_search(self, query: str, top_k: int = 6) -> List[Dict[str, Any]]:
        """
        Combines BM25 and Dense vector search via Reciprocal Rank Fusion (RRF):
        RRF_score(d) = 1 / (60 + rank_bm25) + 1 / (60 + rank_dense)
        """
        bm25_res = self.bm25_search(query, limit=20)
        dense_res = self.dense_search(query, limit=20)
        
        rrf_scores = {}
        doc_map = {}
        
        # Add BM25 ranks
        for rank, item in enumerate(bm25_res):
            did = item["doc_id"]
            doc_map[did] = item
            rrf_scores[did] = rrf_scores.get(did, 0.0) + (1.0 / (60.0 + rank + 1.0))
            
        # Add Dense ranks
        for rank, item in enumerate(dense_res):
            did = item["doc_id"]
            if did not in doc_map:
                doc_map[did] = item
            rrf_scores[did] = rrf_scores.get(did, 0.0) + (1.0 / (60.0 + rank + 1.0))
            
        # Sort by combined RRF score
        sorted_docs = sorted(rrf_scores.items(), key=lambda x: x[1], reverse=True)
        
        final_results = []
        for did, score in sorted_docs[:top_k]:
            item = doc_map[did]
            item["rrf_score"] = score
            final_results.append(item)
            
        return final_results
