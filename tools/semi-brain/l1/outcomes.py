#!/usr/bin/env python3
"""
outcomes.py
The outcome log: for every prompt in this project's Claude Code sessions, what was actually
read and changed before the next prompt. It is what a brief is scored against (was the file
that got edited in the brief?) and what learned ranking trains on.

  turn     a real user prompt (not a tool result, not harness scaffolding) up to the next one
  read     files opened with Read, in the repo
  edited   files changed with Edit/Write/MultiEdit/NotebookEdit, plus every file of a commit
           made in the turn (the "[branch abc1234] ..." line in a Bash result names it), since
           much of the editing here goes through Bash
  paths    repo-relative; paths inside other worktrees of this repo map to the same file
  sources  <session>.jsonl and its subagents/*.jsonl (their tool calls belong to the parent
           turn running at that moment)
  cost     what the turn spent, subagents included - what a brief is meant to save:
             tools    tool calls
             explore  tool calls up to and including the first edit (all of them if none)
             tokens   context tokens processed (input + cache read + cache write), per API call
             out      output tokens
           an API call is counted once, though the transcript repeats it per content block

Written to l1/state/outcomes.jsonl (local only: it holds prompt text). Incremental: a
session is re-read only when one of its files changed size or mtime.

    python3 l1/outcomes.py            # update, print counts
"""

import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from l1 import REPO_PATH, STATE_DIR  # noqa: E402
from l1.research import SCAFFOLD_PREFIXES  # noqa: E402

SESSIONS_DIR = Path.home() / ".claude" / "projects" / ("-" + str(REPO_PATH).strip("/").replace("/", "-"))
OUT = STATE_DIR / "outcomes.jsonl"
WATERMARK = STATE_DIR / "outcomes_seen.json"
EDIT_TOOLS = {"Edit", "Write", "MultiEdit", "NotebookEdit"}
MAX_PROMPT = 2000
SCHEMA = 2  # bump when a turn gains a field: every session is re-read once

PATH_RE = re.compile(rf"^{re.escape(str(REPO_PATH))}[^/]*/(?:\.claude/worktrees/[^/]+/)?(.+)$")
COMMIT_RE = re.compile(r"^\[[^\]\s]+(?: \(root-commit\))? ([0-9a-f]{7,40})\] ", re.M)
REMINDER_RE = re.compile(r"<system-reminder>.*?</system-reminder>", re.S)
SKIP_PREFIXES = SCAFFOLD_PREFIXES + ("<local-command", "<bash-", "<user-prompt-submit-hook",
                                     "[Request interrupted", "Caveat:")


def repo_path(p):
    m = PATH_RE.match(p or "")
    return m.group(1) if m else None


def _ts(rec):
    try:
        return datetime.fromisoformat(rec["timestamp"].replace("Z", "+00:00")).timestamp()
    except (KeyError, ValueError, AttributeError):
        return None


def prompt_text(rec):
    """The user's own text, or None for tool results, meta records and scaffolding."""
    if rec.get("type") != "user" or rec.get("isMeta") or rec.get("isSidechain"):
        return None
    content = (rec.get("message") or {}).get("content")
    if isinstance(content, list):
        if any(c.get("type") == "tool_result" for c in content if isinstance(c, dict)):
            return None
        content = "\n".join(c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text")
    if not isinstance(content, str):
        return None
    text = REMINDER_RE.sub("", content).strip()
    if not text or text.startswith(SKIP_PREFIXES):
        return None
    return text


def _events(path):
    """(t, kind, value) from one transcript: prompt / read / edit / commit / tool / usage."""
    out = []
    try:
        lines = path.open(encoding="utf-8", errors="replace")
    except OSError:
        return out
    with lines:
        for line in lines:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            t = _ts(rec)
            if t is None:
                continue
            text = prompt_text(rec)
            if text is not None:
                out.append((t, "prompt", text))
                continue
            msg = rec.get("message") or {}
            usage = msg.get("usage") if rec.get("type") == "assistant" else None
            if isinstance(usage, dict) and msg.get("id"):
                ctx = sum(usage.get(k) or 0 for k in
                          ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens"))
                out.append((t, "usage", (msg["id"], ctx, usage.get("output_tokens") or 0)))
            content = msg.get("content")
            if not isinstance(content, list):
                continue
            for c in content:
                if not isinstance(c, dict):
                    continue
                if c.get("type") == "tool_use":
                    out.append((t, "tool", c.get("name", "")))
                    inp = c.get("input") or {}
                    f = repo_path(inp.get("file_path") or inp.get("notebook_path"))
                    if f and c.get("name") in EDIT_TOOLS:
                        out.append((t, "edit", f))
                    elif f and c.get("name") == "Read":
                        out.append((t, "read", f))
                elif c.get("type") == "tool_result":
                    body = c.get("content")
                    if isinstance(body, list):
                        body = "\n".join(b.get("text", "") for b in body if isinstance(b, dict))
                    if isinstance(body, str) and "] " in body:
                        for h in COMMIT_RE.findall(body):
                            out.append((t, "commit", h))
    return out


def session_turns(session_id, files):
    main, subs = [], []
    for f in files:
        (subs if f.parent.name == "subagents" else main).extend(_events(f))
    main.sort(key=lambda e: e[0])
    prompts = [e for e in main if e[1] == "prompt"]
    if not prompts:
        return []
    turns = [{"session": session_id, "t": p[0], "t_end": None, "prompt": p[2][:MAX_PROMPT],
              "read": [], "edited": [], "commits": [],
              "tools": 0, "explore": 0, "tokens": 0, "out": 0} for p in prompts]
    for i in range(len(turns) - 1):
        turns[i]["t_end"] = turns[i + 1]["t"]
    starts = [t["t"] for t in turns]
    calls = [{} for _ in turns]  # message id -> (context, output), last record wins
    for t, kind, value in sorted(main + subs, key=lambda e: e[0]):
        if kind == "prompt" or t < starts[0]:
            continue
        i = max(j for j, s in enumerate(starts) if s <= t)
        turn = turns[i]
        if kind == "tool":
            turn["tools"] += 1
            if not turn["edited"]:
                turn["explore"] += 1
            continue
        if kind == "usage":
            calls[i][value[0]] = value[1:]
            continue
        key = {"read": "read", "edit": "edited", "commit": "commits"}[kind]
        if value not in turn[key]:
            turn[key].append(value)
    for turn, c in zip(turns, calls):
        turn["tokens"] = sum(ctx for ctx, _ in c.values())
        turn["out"] = sum(o for _, o in c.values())
    return turns


def resolve_commits(hashes):
    """{short: full} for the hashes that still exist in this repo."""
    if not hashes:
        return {}
    res = subprocess.run(["git", "cat-file", "--batch-check=%(objectname) %(objecttype)"],
                         cwd=REPO_PATH, input="\n".join(hashes) + "\n", capture_output=True, text=True)
    out = {}
    for h, line in zip(hashes, res.stdout.splitlines()):
        parts = line.split()
        if len(parts) == 2 and parts[1] == "commit":
            out[h] = parts[0]
    return out


def update():
    from mine_git_history import commit_files
    seen = json.loads(WATERMARK.read_text()) if WATERMARK.exists() else {}
    if seen.get("_schema") != SCHEMA:
        seen = {}
    old = {}
    if OUT.exists():
        for line in OUT.open():
            r = json.loads(line)
            old.setdefault(r["session"], []).append(r)

    groups = {}
    for f in SESSIONS_DIR.glob("*.jsonl"):
        groups.setdefault(f.stem, []).append(f)
    for f in SESSIONS_DIR.glob("*/subagents/*.jsonl"):
        groups.setdefault(f.parent.parent.name, []).append(f)

    new_seen, changed = {"_schema": SCHEMA}, 0
    for sid, files in groups.items():
        sig = [[str(f), f.stat().st_size, int(f.stat().st_mtime)] for f in sorted(files)]
        new_seen[sid] = sig
        if seen.get(sid) == sig and sid in old:
            continue
        old[sid] = session_turns(sid, files)
        changed += 1

    turns = [t for sid in sorted(old) if sid in groups for t in old[sid]]
    missing = sorted({h for t in turns for h in t["commits"] if not t.get("commit_files_done")})
    full = resolve_commits(missing)
    files_of = commit_files(sorted(set(full.values()))) if full else {}
    for t in turns:
        if t.get("commit_files_done"):
            continue
        for h in t["commits"]:
            for f in files_of.get(full.get(h, ""), []):
                if f not in t["edited"]:
                    t["edited"].append(f)
        t["commit_files_done"] = True

    tmp = OUT.with_suffix(".tmp")
    with tmp.open("w") as fh:
        for t in sorted(turns, key=lambda t: t["t"]):
            fh.write(json.dumps(t) + "\n")
    tmp.replace(OUT)
    WATERMARK.write_text(json.dumps(new_seen))
    return {"sessions": len(groups), "reparsed": changed, "turns": len(turns),
            "turns_with_edits": sum(1 for t in turns if t["edited"])}


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "1_extractors"))
    import time
    t0 = time.time()
    print(update(), f"{time.time() - t0:.1f}s")
