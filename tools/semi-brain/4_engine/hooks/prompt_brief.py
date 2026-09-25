#!/usr/bin/env python3
"""
prompt_brief.py
Claude Code UserPromptSubmit hook: asks the watch daemon's warm brief server
(l5/serve.py) for the semi-brain brief of the prompt and hands it to Claude as
additionalContext. Stdlib only and silent on any failure: no daemon, a timeout or
an error just means no brief, never a blocked prompt.

Skipped: short prompts ("yes", "continue"), slash commands, harness scaffolding. The server
decides the rest: a held-out prompt (l5/serve.py arms) comes back with an empty brief.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from l5.serve import ask  # noqa: E402

MIN_CHARS = 25
SKIP_PREFIXES = ("/", "<command-", "<task-notification", "This session is being continued")


def git_common_dir(cwd):
    """The shared .git directory above cwd, worktrees included, without starting git."""
    for d in [Path(cwd).resolve(), *Path(cwd).resolve().parents]:
        g = d / ".git"
        if g.is_dir():
            return g
        if g.is_file():  # a worktree: "gitdir: <main>/.git/worktrees/<name>"
            wt = Path(g.read_text().split("gitdir:", 1)[1].strip())
            common = wt / "commondir"
            return (wt / common.read_text().strip()).resolve() if common.exists() else wt
    return None


def main():
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return
    prompt = (data.get("prompt") or "").strip()
    if len(prompt) < MIN_CHARS or prompt.startswith(SKIP_PREFIXES):
        return
    try:
        gitdir = git_common_dir(data.get("cwd") or ".")
        if gitdir is None:
            return
        reply = ask(gitdir / "brain_brief.sock", prompt, session=data.get("session_id", ""),
                    hook=True)
    except Exception:
        return
    brief = reply.get("brief")
    if brief:
        print(json.dumps({"hookSpecificOutput": {
            "hookEventName": "UserPromptSubmit",
            "additionalContext": "Semi-brain brief (retrieved automatically; a lead, not a fact):\n" + brief}}))


if __name__ == "__main__":
    main()
