#!/usr/bin/env python3
"""
index_codebase.py
Builds a high-speed SQLite Hybrid Search Index (FTS5 BM25 + FastEmbed Dense Vectors):
1. AST C++ Code Symbols & Signatures (from ast_symbol_graph.json)
2. Git Commits & Historical Fixes (from git_commits_corpus.json)
3. Recovered Historical Design Plans & Prompts (from recovered_docs_corpus.json)
4. Specialized Invariant Skills (from skills_and_invariants_corpus.json)
5. From-Scratch Blueprints (from build_your_own_x_corpus.json)
6. Real Session History - human<->assistant decision turns, from Claude Code
   (session_history_corpus.json) and Antigravity/Gemini (antigravity_history_corpus.json)
7. Session Insights - topic clusters, problem->solution pairs, category stats
   (from session_analysis_corpus.json, produced by analyze_session_linguistics.py)
8. Dev Trajectory - session turns classified against this repo's own Conventional Commit
   type/scope vocabulary and its node-category taxonomy, with weekly trend slopes
   (from dev_trajectory_corpus.json, produced by classify_dev_trajectory.py)

Written as two databases with the same schema:
- knowledge_index.db          - sources 1-5 (code, commits, docs, skills, blueprints).
                                 Built only from what is already public in this repo; tracked in git.
- knowledge_index_private.db  - sources 6-8, derived from local chat/session transcripts.
                                 Never committed or pushed (see tools/semi-brain/.gitignore).
retriever.py searches both.
"""

import sqlite3
import json
import sys
import os
import struct
import numpy as np
from pathlib import Path

EXTRACTORS_OUT = Path(__file__).resolve().parent / "output"
DB_FILE = EXTRACTORS_OUT / "knowledge_index.db"
PRIVATE_DB_FILE = EXTRACTORS_OUT / "knowledge_index_private.db"
# Categories built from local chat/session transcripts - these go to PRIVATE_DB_FILE only.
PRIVATE_CATEGORIES = {"session_history", "session_insight", "dev_trajectory"}

def serialize_vector(vec: np.ndarray) -> bytes:
    """Pack float32 vector into binary bytes."""
    return struct.pack(f"{len(vec)}f", *vec)

def write_index(db_file, documents, embed_model):
    if db_file.exists():
        db_file.unlink()

    print(f"Initializing SQLite database at: {db_file}")
    conn = sqlite3.connect(db_file)
    cur = conn.cursor()

    # 1. Create FTS5 table for BM25 keyword search
    cur.execute("""
        CREATE VIRTUAL TABLE fts_documents USING fts5(
            doc_id UNINDEXED,
            category,
            title,
            content,
            filepath UNINDEXED
        );
    """)

    # 2. Create Dense Embeddings table
    cur.execute("""
        CREATE TABLE vector_documents (
            doc_id TEXT PRIMARY KEY,
            category TEXT,
            title TEXT,
            snippet TEXT,
            filepath TEXT,
            embedding BLOB
        );
    """)

    print(f"Populating FTS5 BM25 index ({len(documents)} documents)...")
    for doc_id, category, title, content, snippet, filepath in documents:
        cur.execute(
            "INSERT INTO fts_documents (doc_id, category, title, content, filepath) VALUES (?, ?, ?, ?, ?)",
            (doc_id, category, title, content, filepath)
        )
    conn.commit()

    # Batch embedding for speed
    batch_size = 256
    all_texts = [d[2] + " " + d[4] for d in documents] # Embed title + snippet

    embedded_count = 0
    for i in range(0, len(all_texts), batch_size):
        batch_texts = all_texts[i:i+batch_size]
        batch_docs = documents[i:i+batch_size]

        vectors = list(embed_model.embed(batch_texts))
        for doc_tuple, vec in zip(batch_docs, vectors):
            doc_id, category, title, content, snippet, filepath = doc_tuple
            cur.execute(
                "INSERT INTO vector_documents (doc_id, category, title, snippet, filepath, embedding) VALUES (?, ?, ?, ?, ?, ?)",
                (doc_id, category, title, snippet, filepath, serialize_vector(vec))
            )
        embedded_count += len(batch_docs)
        print(f"Embedded {embedded_count}/{len(documents)} documents...")

    conn.commit()
    conn.close()
    print(f"✅ SQLite Hybrid Knowledge Index built successfully at: {db_file}")

def load_corpora():
    """Read every corpus index_codebase consumes, keyed by name."""
    print("Loading extracted corpora...")
    commits = []
    if (EXTRACTORS_OUT / "git_commits_corpus.json").exists():
        with open(EXTRACTORS_OUT / "git_commits_corpus.json", "r", encoding="utf-8") as f:
            commits = json.load(f)
            
    ast_data = {}
    if (EXTRACTORS_OUT / "ast_symbol_graph.json").exists():
        with open(EXTRACTORS_OUT / "ast_symbol_graph.json", "r", encoding="utf-8") as f:
            ast_data = json.load(f)
            
    docs = []
    if (EXTRACTORS_OUT / "recovered_docs_corpus.json").exists():
        with open(EXTRACTORS_OUT / "recovered_docs_corpus.json", "r", encoding="utf-8") as f:
            docs = json.load(f)
            
    skills = []
    if (EXTRACTORS_OUT / "skills_and_invariants_corpus.json").exists():
        with open(EXTRACTORS_OUT / "skills_and_invariants_corpus.json", "r", encoding="utf-8") as f:
            skills = json.load(f)
            
    byox = []
    if (EXTRACTORS_OUT / "build_your_own_x_corpus.json").exists():
        with open(EXTRACTORS_OUT / "build_your_own_x_corpus.json", "r", encoding="utf-8") as f:
            byox = json.load(f)

    sessions = []
    if (EXTRACTORS_OUT / "session_history_corpus.json").exists():
        with open(EXTRACTORS_OUT / "session_history_corpus.json", "r", encoding="utf-8") as f:
            sessions = json.load(f)

    if (EXTRACTORS_OUT / "antigravity_history_corpus.json").exists():
        with open(EXTRACTORS_OUT / "antigravity_history_corpus.json", "r", encoding="utf-8") as f:
            sessions.extend(json.load(f))

    session_analysis = {}
    if (EXTRACTORS_OUT / "session_analysis_corpus.json").exists():
        with open(EXTRACTORS_OUT / "session_analysis_corpus.json", "r", encoding="utf-8") as f:
            session_analysis = json.load(f)

    dev_trajectory = {}
    if (EXTRACTORS_OUT / "dev_trajectory_corpus.json").exists():
        with open(EXTRACTORS_OUT / "dev_trajectory_corpus.json", "r", encoding="utf-8") as f:
            dev_trajectory = json.load(f)

    return {
        "commits": commits,
        "ast_data": ast_data,
        "docs": docs,
        "skills": skills,
        "byox": byox,
        "sessions": sessions,
        "session_analysis": session_analysis,
        "dev_trajectory": dev_trajectory,
    }

def prepare_documents(corpora):
    """Corpora -> (doc_id, category, title, content, snippet, filepath) rows. Pure, so the
    replay benchmark can call it on time-restricted corpora."""
    commits = corpora.get("commits") or []
    ast_data = corpora.get("ast_data") or {}
    docs = corpora.get("docs") or []
    skills = corpora.get("skills") or []
    byox = corpora.get("byox") or []
    sessions = corpora.get("sessions") or []
    session_analysis = corpora.get("session_analysis") or {}
    dev_trajectory = corpora.get("dev_trajectory") or {}

    # Prepare documents for indexing
    documents = [] # list of (doc_id, category, title, content, snippet, filepath)
    
    # A. Skills
    for s in skills:
        name = s.get("name", "")
        content = s.get("content", "")
        doc_id = f"skill::{name}"
        documents.append((doc_id, "skill", f"Skill: {name}", content, content[:300], s.get("path", "")))
        
    # B. Recovered Design Plans
    for d in docs:
        path = d.get("path", "")
        content = d.get("content", "")
        doc_id = f"plan::{path}"
        documents.append((doc_id, "design_plan", f"Design Plan: {path}", content, content[:300], path))
        
    # C. Git Commits (filter for meaningful commits)
    for c in commits:
        chash = c.get("hash", "")[:8]
        msg = c.get("parsed_message", {})
        title = msg.get("title", "")
        body = msg.get("body", "")
        bullets = " ".join(msg.get("bullets", []))
        full_text = f"{title}\n{body}\n{bullets}"
        doc_id = f"commit::{chash}"
        documents.append((doc_id, "git_commit", f"Commit [{chash}]: {title}", full_text, full_text[:300], ""))
        
    # D. Key AST Symbols
    symbols = ast_data.get("symbols", {})
    for sym_name, sym_meta in symbols.items():
        doc_id = f"ast::{sym_name}"
        file_path = sym_meta.get("file", "")
        kind = sym_meta.get("kind", "")
        subsys = sym_meta.get("subsystem", "")
        text = f"C++ {kind} {sym_name} in {file_path} subsystem {subsys}"
        documents.append((doc_id, "ast_symbol", f"Symbol: {sym_name}", text, text, file_path))
        
    # E. BYOX Blueprints
    for b in byox:
        cat = b.get("category", "")
        for t in b.get("tutorials", []):
            doc_id = f"byox::{t['title']}"
            text = f"First Principles {cat}: {t['title']} in {t['language']}"
            documents.append((doc_id, "byox_blueprint", f"Blueprint: {t['title']}", text, text, t.get("url", "")))

    # F. Real Session History (what was asked, decided, and why - across every local session,
    # from both Claude Code and Antigravity/Gemini transcripts)
    # Ids number turns within their own session, so a new turn never renumbers anyone else's
    # doc (the incremental index keys on doc_id).
    ordinal = {}
    for turn in sessions:
        session_id = turn.get("session_id", "")
        user_text = turn.get("user_text", "")
        assistant_text = turn.get("assistant_text", "")
        source_tool = turn.get("source_tool", "claude_code")
        n = ordinal[(source_tool, session_id)] = ordinal.get((source_tool, session_id), -1) + 1
        if not user_text.strip():
            continue
        doc_id = f"session::{source_tool}::{session_id}::{n}"
        title = f"Session [{source_tool}] {session_id[:8]}: {user_text[:60]}"
        content = f"{user_text}\n\n{assistant_text}"
        documents.append((doc_id, "session_history", title, content, content[:300], turn.get("cwd", "")))

    # G. Session Insights - topic clusters and problem->solution pairs distilled across every
    # session (memory_category taxonomy: episodic/semantic/procedural/problem_solution). Indexed
    # as their own category so a query can surface "how we usually solve X" / "what this project's
    # sessions are mostly about", not just individual raw turns.
    global_analysis = session_analysis.get("global", {})
    for cluster in global_analysis.get("cluster_summaries", []):
        cluster_id = cluster.get("cluster_id")
        terms = ", ".join(cluster.get("top_terms", []))
        examples = " | ".join(cluster.get("example_turns", []))
        doc_id = f"topic::{cluster_id}"
        title = f"Session Topic Cluster {cluster_id}: {terms}"
        content = (
            f"Topic cluster of {cluster.get('size', 0)} session turns, mostly "
            f"{cluster.get('dominant_category', '')}. Key terms: {terms}. Example turns: {examples}"
        )
        documents.append((doc_id, "session_insight", title, content, content[:300], ""))

    pair_ordinal = {}
    for pair in global_analysis.get("problem_solution_pairs", []):
        sid = pair.get("session_id", "")
        n = pair_ordinal[sid] = pair_ordinal.get(sid, -1) + 1
        doc_id = f"problem_solution::{sid}::{n}"
        title = f"Problem -> Solution: {pair.get('problem_summary', '')[:60]}"
        content = f"Problem: {pair.get('problem_summary', '')}\n\nSolution: {pair.get('solution_summary', '')}"
        documents.append((doc_id, "session_insight", title, content, content[:300], ""))

    # H. Dev Trajectory - per-scope/work_type/node_category summaries with weekly trend slopes,
    # so a query like "is Field work trending up" or "what kind of work is arrange scope" can be
    # answered from this repo's own commit-style taxonomy rather than a generic topic cluster.
    def _trend_sentence(kind, label, info):
        slope = info.get("slope", 0)
        direction = "rising" if slope > 0.3 else "falling" if slope < -0.3 else "flat"
        counts = info.get("weekly_counts", [])
        return (
            f"{kind} '{label}' is {direction} over the last {len(counts)} weeks "
            f"(slope {slope}, weekly turn counts: {counts})."
        )

    for scope, info in dev_trajectory.get("scope_trends", {}).items():
        doc_id = f"trajectory::scope::{scope}"
        title = f"Dev Trajectory - scope: {scope}"
        content = _trend_sentence("Scope", scope, info)
        documents.append((doc_id, "dev_trajectory", title, content, content[:300], ""))

    for wt, info in dev_trajectory.get("work_type_trends", {}).items():
        doc_id = f"trajectory::work_type::{wt}"
        title = f"Dev Trajectory - work type: {wt}"
        content = _trend_sentence("Work type", wt, info)
        documents.append((doc_id, "dev_trajectory", title, content, content[:300], ""))

    for nc, info in dev_trajectory.get("node_category_trends", {}).items():
        doc_id = f"trajectory::node_category::{nc}"
        title = f"Dev Trajectory - node category: {nc}"
        content = _trend_sentence("Node category", nc, info)
        documents.append((doc_id, "dev_trajectory", title, content, content[:300], ""))
    return documents

def build_hybrid_index():
    """Update both index DBs in place from the corpora: only docs whose content changed are
    re-inserted, and their vectors come from the shared L1 cache (a text is embedded once)."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from l1.outputs import reconcile
    from l1.store import Store
    from l1.vector_cache import VectorCache

    EXTRACTORS_OUT.mkdir(parents=True, exist_ok=True)
    documents = prepare_documents(load_corpora())
    print(f"Total documents prepared for hybrid index: {len(documents)}")
    store, cache = Store(), VectorCache()
    added, changed, deleted = store.sync_docs(documents, PRIVATE_CATEGORIES)
    print(f"Doc store: {added} added, {changed} changed, {deleted} deleted")
    for db, private in ((DB_FILE, False), (PRIVATE_DB_FILE, True)):
        ins, dele = reconcile(db, store, private, cache)
        print(f"{db.name}: {ins} rows written, {dele} removed")
    print(f"Embedded {cache.misses} new texts ({cache.hits} from cache)")

if __name__ == "__main__":
    build_hybrid_index()
