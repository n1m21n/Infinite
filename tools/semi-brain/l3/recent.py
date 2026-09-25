#!/usr/bin/env python3
"""
recent.py
The work-in-progress prior: most prompts continue recent work, so the files edited just before
a prompt are strong candidates for it. Two rankings, both from events strictly before `now`:

  session  files edited earlier in the same Claude Code session, most recent turn first
  recent   files edited anywhere lately (outcome-log turns and commits), each edit weighted
           exp(-age / TAU_DAYS)

  events   outcome-log turns (l1/state/outcomes.jsonl: session, t, edited) and commits
           (time, files). Paths outside src/ and the HUB_FILES are ignored: they are not what
           a brief should point at, and main.cpp would win every time.
  embargo  events newer than now - embargo are left out; the replay uses it so a fix's own
           conversation cannot hand the brain its answer. Live it is ~0 (the sync lag).
"""

import json
import math
from datetime import datetime
from pathlib import Path

TAU_DAYS = 2.0
HUB_FILES = ("src/main.cpp",)
RECENT_TOP = 30
CODE_EXT = (".cpp", ".h", ".mm", ".hpp", ".c", ".m")


def commit_time(c):
    """Unix time of a commit record ("Fri Sep 25 12:06:33 2026 +0530" author_date)."""
    try:
        return datetime.strptime(c["author_date"], "%a %b %d %H:%M:%S %Y %z").timestamp()
    except (KeyError, ValueError, TypeError):
        return None


def _keep(f):
    return f.startswith("src/") and f.endswith(CODE_EXT) and f not in HUB_FILES


class RecentWork:
    def __init__(self, turns=(), commits=()):
        """turns: dicts with session, t, edited. commits: (time, [files])."""
        ev = [(t["t"], t.get("session", ""), [f for f in t.get("edited", ()) if _keep(f)]) for t in turns]
        ev += [(t, "", [f for f in files if _keep(f)]) for t, files in commits]
        self.events = sorted(e for e in ev if e[2])

    @classmethod
    def load(cls, state_dir, commits_corpus=()):
        turns = []
        p = Path(state_dir) / "outcomes.jsonl"
        if p.exists():
            with p.open() as fh:
                for line in fh:
                    r = json.loads(line)
                    turns.append({"t": r["t"], "session": r["session"], "edited": r["edited"]})
        commits = []
        for c in commits_corpus:
            t = commit_time(c)
            if t and c.get("files"):
                commits.append((t, c["files"]))
        return cls(turns, commits)

    def rankings(self, now, session="", embargo=0.0):
        cut = now - embargo
        sess, recent = {}, {}
        for t, s, files in self.events:
            if t >= cut:
                break
            w = math.exp(-(now - t) / 86400.0 / TAU_DAYS)
            for f in files:
                recent[f] = recent.get(f, 0.0) + w
                if session and s == session:
                    sess[f] = t  # latest edit time in this session
        return (sorted(sess, key=sess.get, reverse=True),
                sorted(recent, key=recent.get, reverse=True)[:RECENT_TOP])
