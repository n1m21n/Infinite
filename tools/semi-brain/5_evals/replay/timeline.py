#!/usr/bin/env python3
"""
timeline.py
Leave-future-out corpora: everything index_codebase.prepare_documents consumes, as it stood
just before a given commit.

  git commits      commits with committer time < case time (the fix itself excluded)
  AST graph        every src/ code file at the fix's parent, parsed (cached per blob)
  skills/AGENTS    .claude/skills/*/SKILL.md and AGENTS.md at the parent
  research docs    docs/prior-art, docs/fix-briefs, docs/reference at the parent
  recovered docs   docs whose deleting commit predates the case
  BYOX             static, unchanged
  sessions         turns older than case time minus an embargo (default 12 h), so the
                   conversation that produced the fix cannot hand the brain its answer
  session insight  problem->solution pairs recomputed from those turns only; the k-means
                   topic clusters and dev-trajectory trends are left out (they are fitted on
                   the whole history, so every one of them would leak the future)
"""

import json
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import (BlobReader, ParseCache, SEMI_BRAIN_DIR, code_files_at, graph_order,  # noqa: E402
                     list_commits, ls_tree)

OUT = SEMI_BRAIN_DIR / "1_extractors" / "output"


def _load(name, default):
    p = OUT / name
    if not p.exists():
        return default
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def parse_ts(ts):
    if not ts:
        return None
    try:
        return datetime.fromisoformat(str(ts).replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


class Timeline:
    def __init__(self, embargo_hours=12.0):
        from mine_git_history import commit_files, parse_commit_message
        from build_ast_graph import assemble_graph
        from analyze_session_linguistics import problem_solution_tag, build_problem_solution_pairs
        self._assemble = assemble_graph
        self._tag = problem_solution_tag
        self._pairs = build_problem_solution_pairs
        self.embargo = embargo_hours * 3600.0

        self.commits = []
        files = commit_files()
        for c in list_commits("HEAD"):
            self.commits.append((c["time"], {
                "hash": c["hash"], "author_name": c["author_name"], "author_email": c["author_email"],
                "author_date": c["author_date"], "parsed_message": parse_commit_message(c["subject"], c["body"]),
                "files": files.get(c["hash"], []),
            }))
        self.commit_time = {rec["hash"]: t for t, rec in self.commits}

        self.recovered = _load("recovered_docs_corpus.json", [])
        self.byox = _load("build_your_own_x_corpus.json", [])
        turns = _load("session_history_corpus.json", [])
        for t in turns:
            t.setdefault("source_tool", "claude_code")
        turns.extend(_load("antigravity_history_corpus.json", []))
        self.turns = [(parse_ts(t.get("timestamp")), t) for t in turns]

        self.reader = BlobReader()
        self.parse = ParseCache()

    def ast_at(self, commit):
        results = [self.parse.get(p, b, self.reader) for p, b in graph_order(code_files_at(commit))]
        return self._assemble(results)

    def skills_at(self, commit):
        records = []
        for path, blob in ls_tree(commit, "AGENTS.md", ".claude/skills"):
            parts = path.split("/")
            if path == "AGENTS.md":
                records.append({"type": "agents_rule", "name": "AGENTS.md", "path": path,
                                "content": self.reader.read(blob).decode("utf-8", "replace")})
            elif len(parts) == 4 and parts[3] == "SKILL.md":
                records.append({"type": "skill", "name": parts[2], "path": path,
                                "content": self.reader.read(blob).decode("utf-8", "replace")})
        return records

    def research_at(self, commit):
        from l1.research import RESEARCH_DIRS, is_research_doc
        return [{"path": path, "content": self.reader.read(blob).decode("utf-8", "replace")}
                for path, blob in ls_tree(commit, *RESEARCH_DIRS) if is_research_doc(path)]

    def corpora_at(self, case):
        cutoff = case["time"]
        sessions = [t for ts, t in self.turns if ts is not None and ts < cutoff - self.embargo]
        tagged = [{"session_id": t.get("session_id", ""), "user_text": t.get("user_text", ""),
                   "assistant_text": t.get("assistant_text", ""),
                   "problem_solution_tag": self._tag(t.get("user_text", ""), t.get("assistant_text", ""))}
                  for t in sessions]
        return {
            "commits": [rec for t, rec in self.commits if t < cutoff and rec["hash"] != case["commit"]],
            "ast_data": self.ast_at(case["parent"]),
            "docs": [d for d in self.recovered
                     if self.commit_time.get(d.get("deleted_in_commit"), float("inf")) < cutoff],
            "skills": self.skills_at(case["parent"]),
            "research_docs": self.research_at(case["parent"]),
            "byox": self.byox,
            "sessions": sessions,
            "session_analysis": {"global": {"problem_solution_pairs": self._pairs(tagged)[:500]}},
            "dev_trajectory": {},
        }
