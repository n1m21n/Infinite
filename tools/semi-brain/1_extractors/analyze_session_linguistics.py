#!/usr/bin/env python3
"""
analyze_session_linguistics.py
Turns the raw session_history_corpus.json (what mine_session_history.py extracts) into a
structured understanding of how work actually happens across sessions, instead of leaving it
as a flat list of turns for keyword search to stumble over.

Grounding for the approach (see docs/plans/... none needed - this is a standard, published
pattern, not invented here):
  - Categorization follows Tulving's classic memory taxonomy (episodic / semantic / procedural),
    the same three-way split systems like MemGPT and Mem0 use to decide what to keep and how to
    retrieve it: episodic = what happened in this session, semantic = a fact/decision/definition,
    procedural = how to do something (a recipe, a fix, a workflow step).
  - Problem/solution pairing mirrors Mem0's ADD/UPDATE extraction step: a turn tagged "problem"
    is linked to the next "solution"-tagged turn in the *same session* - most sessions are one
    continuous debugging arc, so adjacency is a good enough heuristic without an LLM call.
  - Topics are found with k-means over FastEmbed embeddings rather than LDA or BERTopic: at
    ~2-3k turns the corpus is too small and too informal (chat, not prose) for LDA's word-count
    generative model, and too small for BERTopic's HDBSCAN step, which discards outliers and
    needs a larger corpus to find stable density clusters. k-means over dense embeddings degrades
    gracefully at this size and needs no new dependency (fastembed + numpy are already in use by
    index_codebase.py).

No sentence-structure model (spaCy et al) is used. Sentence/imperative/question shape is
recovered with cheap regex + a curated verb list matched against the vocabulary this project's
sessions actually use (see IMPERATIVE_VERBS below) - accurate enough for these purposes and adds
zero new dependencies.

Output: session_analysis_corpus.json - {turns: [...], global: {...}}. Consumed by
index_codebase.py as a 7th document category ("session_insight") so retrieval can surface
"how we usually approach X" / "past problems like this and how they were solved", not just raw
transcript snippets.
"""

import json
import re
import os
from collections import Counter
import sys
from pathlib import Path

import numpy as np

EXTRACTORS_OUT = Path(__file__).resolve().parent / "output"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
INPUT_FILE = EXTRACTORS_OUT / "session_history_corpus.json"
ANTIGRAVITY_INPUT_FILE = EXTRACTORS_OUT / "antigravity_history_corpus.json"
OUTPUT_FILE = EXTRACTORS_OUT / "session_analysis_corpus.json"

EMBED_TEXT_CHARS = 500        # per turn, for clustering - title-length context is enough
NUM_CLUSTERS_DIVISOR = 40     # ~1 cluster per 40 turns, clamped below
MIN_CLUSTERS, MAX_CLUSTERS = 6, 40
KMEANS_ITERS = 25
TOP_TERMS_PER_CLUSTER = 8
TOP_WORDS_GLOBAL = 150

STOPWORDS = set("""
a about above after again against all am an and any are aren't as at be because been before
being below between both but by can't cannot could couldn't did didn't do does doesn't doing
don't down during each few for from further had hadn't has hasn't have haven't having he he'd
he'll he's her here here's hers herself him himself his how how's i i'd i'll i'm i've if in into
is isn't it it's its itself let's me more most mustn't my myself no nor not of off on once only
or other ought our ours ourselves out over own same shan't she she'd she'll she's should
shouldn't so some such than that that's the their theirs them themselves then there there's
these they they'd they'll they're they've this those through to too under until up very was
wasn't we we'd we'll we're we've were weren't what what's when when's where where's which while
who who's whom why why's with won't would wouldn't you you'd you'll you're you've your yours
yourself yourselves it's im dont didnt doesnt youre thats whats theres this's just now also
okay ok yes yeah sure got get gets getting one two also still really actually right left going
lets
""".split())

IMPERATIVE_VERBS = set("""
fix add make create wire remove delete implement run rebuild commit merge push pull test verify
explain investigate look check read write update review build sweep keep let move rename
refactor split extract simplify clean cleanup revert undo redo debug trace grep search find
audit design plan draft finish continue resume start stop pause open close save load apply
compile deploy release ship publish document comment annotate rewrite replace insert append
prepend wrap unwrap flatten expand collapse toggle enable disable configure set reset restore
""".split())

QUESTION_STARTERS = set("""
what why how when where who which is are do does did can could should would will won't isn't
aren't doesn't don't didn't wasn't weren't
""".split())

PROBLEM_KEYWORDS = set("""
error bug crash broken issue fails failing failed failure wrong doesn't isn't crashes stuck
stale missing incorrect misaligned regression glitch bleeding artifact leak race deadlock
undefined nan overflow silent breaks freezes hangs corrupt corrupted duplicate desync
""".split())

SOLUTION_SINGLE_WORDS = set("""
fixed resolved solved patched corrected restored addressed traced
""".split())
SOLUTION_PHRASES = [
    "root cause", "turns out", "it works now", "works now", "figured out",
    "the issue was", "the reason was", "the problem was", "that fixed it",
]

CODE_TOKEN_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+\b|\b[a-z][a-zA-Z0-9]*[A-Z][a-zA-Z0-9]*\b|\b[A-Za-z_][a-z0-9_]*_[a-z0-9_]+\b")
FILE_TOKEN_RE = re.compile(r"\b[\w./-]+\.(?:cpp|h|hpp|py|md|json|jsonl|cmake|txt|sh)\b")
SENTENCE_SPLIT_RE = re.compile(r"(?<=[.!?])\s+")
WORD_RE = re.compile(r"[a-zA-Z']+")
FENCE_RE = re.compile(r"```")


def tokenize_words(text):
    return [w.lower() for w in WORD_RE.findall(text)]


def sentence_stats(text):
    sentences = [s.strip() for s in SENTENCE_SPLIT_RE.split(text.strip()) if s.strip()]
    if not sentences:
        return {"count": 0, "avg_words": 0.0, "questions": 0, "imperatives": 0}
    questions = 0
    imperatives = 0
    total_words = 0
    for s in sentences:
        words = tokenize_words(s)
        total_words += len(words)
        if s.endswith("?") or (words and words[0] in QUESTION_STARTERS):
            questions += 1
        elif words and words[0] in IMPERATIVE_VERBS:
            imperatives += 1
    return {
        "count": len(sentences),
        "avg_words": round(total_words / len(sentences), 1),
        "questions": questions,
        "imperatives": imperatives,
    }


def code_signals(user_text, assistant_text):
    combined = user_text + "\n" + assistant_text
    return {
        "code_fences": len(FENCE_RE.findall(combined)) // 2,
        "code_identifiers": len(set(CODE_TOKEN_RE.findall(combined))),
        "files_mentioned": sorted(set(FILE_TOKEN_RE.findall(combined)))[:20],
    }


def problem_solution_tag(user_text, assistant_text):
    combined_lower = (user_text + " " + assistant_text).lower()
    words = set(re.findall(r"[a-z']+", combined_lower))
    has_problem = bool(words & PROBLEM_KEYWORDS)
    # Word-boundary matching, not substring - "works" as a bare `in` check also matches inside
    # "frameworks"/"networks", which are common enough in this codebase's vocabulary to have
    # swamped the classifier (92% of turns mis-tagged problem_solution on the first run).
    has_solution = bool(words & SOLUTION_SINGLE_WORDS) or any(
        re.search(r"\b" + re.escape(phrase) + r"\b", combined_lower) for phrase in SOLUTION_PHRASES
    )
    if has_problem and has_solution:
        return "problem_and_solution"
    if has_problem:
        return "problem"
    if has_solution:
        return "solution"
    return "neither"


def memory_category(user_text, u_stats, tag):
    """Tulving-style split: episodic (something was done/happened), semantic (a fact or
    decision was asked/stated), procedural (how to do something), problem_solution (a bug's
    lifecycle). Order matters - problem/solution and procedural are checked before the more
    generic episodic/semantic fallback."""
    if tag in ("problem", "solution", "problem_and_solution"):
        return "problem_solution"
    lower = user_text.lower()
    if u_stats["imperatives"] > 0 and any(k in lower for k in ("how", "step", "workflow", "process", "procedure")):
        return "procedural"
    if u_stats["questions"] > 0 or lower.strip().startswith(("what", "why", "how", "is ", "are ", "does", "do ")):
        return "semantic"
    if u_stats["imperatives"] > 0:
        return "episodic"
    return "episodic"


def kmeans_numpy(vectors, k, iters=KMEANS_ITERS, seed=7):
    rng = np.random.default_rng(seed)
    n = vectors.shape[0]
    k = min(k, n)
    # k-means++ style seeding for stabler clusters than pure random init
    centroids = np.empty((k, vectors.shape[1]), dtype=np.float32)
    first = rng.integers(0, n)
    centroids[0] = vectors[first]
    closest_sq = np.full(n, np.inf)
    for i in range(1, k):
        dist_sq = np.sum((vectors - centroids[i - 1]) ** 2, axis=1)
        closest_sq = np.minimum(closest_sq, dist_sq)
        probs = closest_sq / (closest_sq.sum() + 1e-12)
        centroids[i] = vectors[rng.choice(n, p=probs)]

    assignments = np.zeros(n, dtype=np.int32)
    for it in range(iters):
        # Chunked distance computation (n x d against each centroid one at a time) instead of a
        # single n*k*d broadcast array - keeps peak memory at O(n*d) regardless of k, which
        # matters on a memory-constrained machine.
        dists = np.empty((n, k), dtype=np.float32)
        for c in range(k):
            diff = vectors - centroids[c]
            dists[:, c] = np.einsum("ij,ij->i", diff, diff)
        new_assignments = np.argmin(dists, axis=1)
        if np.array_equal(new_assignments, assignments):
            break
        assignments = new_assignments
        for c in range(k):
            mask = assignments == c
            if mask.any():
                centroids[c] = vectors[mask].mean(axis=0)
        print(f"  k-means iteration {it + 1}/{iters}", flush=True)
    return assignments, centroids


def top_terms_for_cluster(member_texts, global_freq, top_n=TOP_TERMS_PER_CLUSTER):
    """Cheap class-based TF-IDF: score each word by how over-represented it is in this
    cluster vs. the whole corpus, so cluster labels read as distinctive terms, not just
    "the"/"node"/"fix" repeated everywhere."""
    local_counts = Counter()
    for text in member_texts:
        local_counts.update(w for w in tokenize_words(text) if w not in STOPWORDS and len(w) > 2)
    total_local = sum(local_counts.values()) or 1
    total_global = sum(global_freq.values()) or 1
    scored = []
    for word, count in local_counts.items():
        local_rate = count / total_local
        global_rate = global_freq.get(word, 0) / total_global
        score = local_rate / (global_rate + 1e-6)
        scored.append((score * count, word))
    scored.sort(reverse=True)
    return [w for _, w in scored[:top_n]]


def build_problem_solution_pairs(turns):
    pairs = []
    by_session = {}
    for t in turns:
        by_session.setdefault(t["session_id"], []).append(t)
    for session_id, session_turns in by_session.items():
        pending_problem = None
        for t in session_turns:
            tag = t["problem_solution_tag"]
            if tag in ("problem", "problem_and_solution"):
                pending_problem = t
            if tag in ("solution", "problem_and_solution") and pending_problem is not None:
                pairs.append({
                    "session_id": session_id,
                    "problem_summary": pending_problem["user_text"][:200],
                    "solution_summary": t["assistant_text"][:300] or t["user_text"][:200],
                })
                pending_problem = None
    return pairs


def analyze(cache=None):
    if not INPUT_FILE.exists():
        print(f"No session history corpus at {INPUT_FILE}; run mine_session_history.py first.")
        return

    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        raw_turns = json.load(f)

    # Claude Code turns don't carry a "source_tool" field; tag them explicitly so mixed-tool
    # analysis (and any future per-tool breakdown) can tell the two apart.
    for t in raw_turns:
        t.setdefault("source_tool", "claude_code")

    if ANTIGRAVITY_INPUT_FILE.exists():
        with open(ANTIGRAVITY_INPUT_FILE, "r", encoding="utf-8") as f:
            antigravity_turns = json.load(f)
        raw_turns.extend(antigravity_turns)

    print(f"Analyzing {len(raw_turns)} session turns...", flush=True)

    global_word_freq = Counter()
    turns = []
    for idx, t in enumerate(raw_turns):
        user_text = t.get("user_text", "")
        assistant_text = t.get("assistant_text", "")
        u_stats = sentence_stats(user_text)
        a_stats = sentence_stats(assistant_text)
        tag = problem_solution_tag(user_text, assistant_text)
        category = memory_category(user_text, u_stats, tag)
        code = code_signals(user_text, assistant_text)
        words = [w for w in tokenize_words(user_text + " " + assistant_text) if w not in STOPWORDS and len(w) > 2]
        global_word_freq.update(words)

        turns.append({
            "idx": idx,
            "session_id": t.get("session_id", ""),
            "timestamp": t.get("timestamp", ""),
            "source_tool": t.get("source_tool", "claude_code"),
            "user_text": user_text,
            "assistant_text": assistant_text,
            "user_sentence_stats": u_stats,
            "assistant_sentence_stats": a_stats,
            "problem_solution_tag": tag,
            "memory_category": category,
            "code_signals": code,
        })

    embed_texts = [(t["user_text"] + " " + t["assistant_text"])[:EMBED_TEXT_CHARS] for t in turns]

    # Embedding ~2.7k turns takes 30-40 minutes under memory pressure. The shared L1 vector cache
    # (keyed by the text itself) means only turns never seen before are embedded; the old
    # whole-array .npy cache was thrown away every time the turn count changed.
    if cache is None:
        from l1.vector_cache import VectorCache
        cache = VectorCache()
    hits0, misses0 = cache.hits, cache.misses
    vectors = np.array(cache.get_many(embed_texts), dtype=np.float32).reshape(len(embed_texts), -1)
    print(f"Embeddings: {cache.hits - hits0} cached, {cache.misses - misses0} new", flush=True)

    k = max(MIN_CLUSTERS, min(MAX_CLUSTERS, len(turns) // NUM_CLUSTERS_DIVISOR))
    print(f"Clustering into {k} topics via k-means...")
    assignments, _ = kmeans_numpy(vectors, k)

    clusters = {}
    for t, cluster_id in zip(turns, assignments):
        t["topic_cluster"] = int(cluster_id)
        clusters.setdefault(int(cluster_id), []).append(t)

    cluster_summaries = []
    for cluster_id, members in sorted(clusters.items()):
        member_texts = [m["user_text"] + " " + m["assistant_text"] for m in members]
        terms = top_terms_for_cluster(member_texts, global_word_freq)
        category_counts = Counter(m["memory_category"] for m in members)
        cluster_summaries.append({
            "cluster_id": cluster_id,
            "size": len(members),
            "top_terms": terms,
            "dominant_category": category_counts.most_common(1)[0][0] if category_counts else "episodic",
            "example_turns": [m["user_text"][:120] for m in members[:3]],
        })

    problem_solution_pairs = build_problem_solution_pairs(turns)
    category_counts = Counter(t["memory_category"] for t in turns)
    tag_counts = Counter(t["problem_solution_tag"] for t in turns)

    output = {
        "turns": turns,
        "global": {
            "total_turns": len(turns),
            "top_words": global_word_freq.most_common(TOP_WORDS_GLOBAL),
            "memory_category_counts": dict(category_counts),
            "problem_solution_tag_counts": dict(tag_counts),
            "num_clusters": k,
            "cluster_summaries": cluster_summaries,
            "problem_solution_pairs": problem_solution_pairs[:500],
        },
    }

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)

    print(f"Categories: {dict(category_counts)}")
    print(f"Problem/solution tags: {dict(tag_counts)}")
    print(f"Found {len(problem_solution_pairs)} problem->solution pairs across sessions")
    print(f"Built {k} topic clusters")
    print(f"Wrote analysis to {OUTPUT_FILE}")


if __name__ == "__main__":
    analyze()
