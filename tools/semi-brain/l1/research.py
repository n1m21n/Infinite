#!/usr/bin/env python3
"""
research.py
The research compartment: what was learned from outside the code, kept apart from commits and
raw chat so a query can reach it directly.

  research_doc      docs/prior-art (prior-art-scout reports), docs/fix-briefs (write-fix-brief
                    output), docs/reference (hand-written reference). Public, like the repo.
  session_research  research explainers mined from past sessions: turns where the ask was to
                    research / explain / compare something and the answer was a long structured
                    write-up (tables, sections, math). Private: session text never leaves the
                    machine, and the category name contains "session" so the pre-push hook
                    refuses it in the public index.
"""

import re
from pathlib import Path

RESEARCH_DIRS = ("docs/prior-art/", "docs/fix-briefs/", "docs/reference/")
MIN_EXPLAINER_CHARS = 1500

ASK_RE = re.compile(
    r"\b(research|researched|paper|papers|arxiv|explain|explainer|explained|deep dive|dig into|"
    r"compare|comparison|how does .{1,40} work|what is (?!left|done|next|remaining)|"
    r"algorithm|algorithms|prior art|state of the art|open source|look into|survey|theory|math)\b", re.I)
STRUCTURE_RE = re.compile(r"(?m)^(#{1,4} |\|.*\|\s*$|\*\*[^*\n]{3,60}\*\*\s*$|\d+\. |- )")
# Harness scaffolding that starts many "user" turns and says nothing about intent.
SCAFFOLD_PREFIXES = ("This session is being continued", "<command-", "<task-notification",
                     "Base directory for this skill")


def is_research_doc(path):
    return path.endswith(".md") and path.startswith(RESEARCH_DIRS)


def load_research_docs(repo):
    out = []
    for d in RESEARCH_DIRS:
        for p in sorted((Path(repo) / d).rglob("*.md")):
            out.append({"path": str(p.relative_to(repo)), "content": p.read_text(encoding="utf-8", errors="replace")})
    return out


def is_explainer(turn):
    user = turn.get("user_text", "") or ""
    answer = turn.get("assistant_text", "") or ""
    if len(answer) < MIN_EXPLAINER_CHARS or user.lstrip().startswith(SCAFFOLD_PREFIXES):
        return False
    if not ASK_RE.search(user[:300]):
        return False
    return len(STRUCTURE_RE.findall(answer)) >= 4


def research_documents(corpora):
    """(doc_id, category, title, content, snippet, filepath) rows for both research categories."""
    rows = []
    for d in corpora.get("research_docs") or []:
        path, content = d["path"], d["content"]
        first = next((l.lstrip("# ").strip() for l in content.splitlines() if l.strip()), path)
        rows.append((f"research::{path}", "research_doc", f"Research: {first[:80]}",
                     content, content[:300], path))
    ordinal = {}
    for turn in corpora.get("sessions") or []:
        key = (turn.get("source_tool", "claude_code"), turn.get("session_id", ""))
        n = ordinal[key] = ordinal.get(key, -1) + 1
        if not is_explainer(turn):
            continue
        tool, sid = key
        # Same title and text as the session_history doc it replaces (index_codebase skips
        # explainer turns there): only the category moves, so ranking is unchanged and the
        # compartment can still be filtered or boosted on its own.
        user, answer = turn["user_text"], turn.get("assistant_text", "")
        content = f"{user}\n\n{answer}"
        rows.append((f"explainer::{tool}::{sid}::{n}", "session_research",
                     f"Session [{tool}] {sid[:8]}: {user[:60]}", content, content[:300], turn.get("cwd", "")))
    return rows
