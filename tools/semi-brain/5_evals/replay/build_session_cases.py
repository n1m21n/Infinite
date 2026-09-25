#!/usr/bin/env python3
"""
build_session_cases.py
Replay cases from real prompts: each turn of the outcome log (l1/outcomes.py) whose prompt
changed src/ files becomes a case scored like a fix commit - the prompt is the query, the
edited src/ files are the targets. Commit cases ask "which files did this fix touch" with the
commit's own words; these ask it in the words the prompt hook actually sees.

  kept      prompt >= MIN_PROMPT chars, 1..MAX_TARGETS src/ files edited
  parent    the newest commit on HEAD older than the prompt (AST/skills as they stood then)
  symbols   only for turns that committed: the innermost function/class touched by the diff
            from `parent` to the turn's last commit (build_cases.target_symbols); turns that
            never committed have no line-level record in outcomes.jsonl, so target_symbols
            stays [] and replay.py's symbol metrics simply skip that case, same as it already
            does for a commit case with no resolvable symbol
  output    l1/state/session_cases.json - local only, it holds prompt text

    python3 build_session_cases.py      # then: replay.py --cases sessions --label X
"""

import bisect
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import SEMI_BRAIN_DIR, BlobReader, ParseCache, git, list_commits  # noqa: E402
from build_cases import target_symbols  # noqa: E402

STATE = SEMI_BRAIN_DIR / "l1" / "state"
OUT = STATE / "session_cases.json"
MIN_PROMPT = 40
MAX_TARGETS = 10
CODE_EXT = (".cpp", ".h", ".mm", ".hpp", ".c", ".m")


def turn_target_symbols(parse, reader, parent, turn_commits, targets):
    """Target symbols for a turn that committed: the innermost function/class touched by the
    cumulative diff from `parent` to the turn's last commit (build_cases.target_symbols per
    file, same technique fix-commit cases use). [] for turns with no commits - outcomes.jsonl
    has no line-level data for edits that were never committed, so there is nothing to diff."""
    if not turn_commits:
        return []
    last = turn_commits[-1]
    syms = []
    for path in targets:
        try:
            blob = git("rev-parse", f"{parent}:{path}").decode().strip()
            for s in target_symbols(parse, reader, parent, last, path, blob):
                if s not in syms:
                    syms.append(s)
        except Exception:
            continue  # file didn't exist at `parent` (added this turn), or diff/parse failed
    return syms


def main():
    commits = sorted((c["time"], c["hash"]) for c in list_commits("HEAD"))
    times = [t for t, _ in commits]
    reader, parse = BlobReader(), ParseCache()
    cases = []
    for line in open(STATE / "outcomes.jsonl"):
        turn = json.loads(line)
        targets = [f for f in turn["edited"] if f.startswith("src/") and f.endswith(CODE_EXT)]
        prompt = turn["prompt"].strip()
        if len(prompt) < MIN_PROMPT or not (1 <= len(targets) <= MAX_TARGETS):
            continue
        i = bisect.bisect_left(times, turn["t"]) - 1
        if i < 0:
            continue
        parent = commits[i][1]
        syms = turn_target_symbols(parse, reader, parent, turn.get("commits", []), targets)
        cases.append({"commit": f"session:{turn['session'][:8]}:{int(turn['t'])}", "parent": parent,
                      "session": turn["session"],
                      "time": turn["t"], "subject": prompt.splitlines()[0][:80],
                      "query_full": prompt, "query_subject": prompt[:200],
                      "target_files": targets, "target_symbols": syms})
    reader.close()
    OUT.write_text(json.dumps(cases, indent=1))
    n_sym = sum(1 for x in cases if x["target_symbols"])
    print(f"{len(cases)} session cases ({n_sym} with target symbols) -> {OUT}")


if __name__ == "__main__":
    main()
