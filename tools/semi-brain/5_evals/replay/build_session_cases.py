#!/usr/bin/env python3
"""
build_session_cases.py
Replay cases from real prompts: each turn of the outcome log (l1/outcomes.py) whose prompt
changed src/ files becomes a case scored like a fix commit - the prompt is the query, the
edited src/ files are the targets. Commit cases ask "which files did this fix touch" with the
commit's own words; these ask it in the words the prompt hook actually sees.

  kept      prompt >= MIN_PROMPT chars, 1..MAX_TARGETS src/ files edited
  parent    the newest commit on HEAD older than the prompt (AST/skills as they stood then)
  output    l1/state/session_cases.json - local only, it holds prompt text

    python3 build_session_cases.py      # then: replay.py --cases sessions --label X
"""

import bisect
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import SEMI_BRAIN_DIR, list_commits  # noqa: E402

STATE = SEMI_BRAIN_DIR / "l1" / "state"
OUT = STATE / "session_cases.json"
MIN_PROMPT = 40
MAX_TARGETS = 10
CODE_EXT = (".cpp", ".h", ".mm", ".hpp", ".c", ".m")


def main():
    commits = sorted((c["time"], c["hash"]) for c in list_commits("HEAD"))
    times = [t for t, _ in commits]
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
        cases.append({"commit": f"session:{turn['session'][:8]}:{int(turn['t'])}", "parent": commits[i][1],
                      "time": turn["t"], "subject": prompt.splitlines()[0][:80],
                      "query_full": prompt, "query_subject": prompt[:200],
                      "target_files": targets, "target_symbols": []})
    OUT.write_text(json.dumps(cases, indent=1))
    print(f"{len(cases)} session cases -> {OUT}")


if __name__ == "__main__":
    main()
