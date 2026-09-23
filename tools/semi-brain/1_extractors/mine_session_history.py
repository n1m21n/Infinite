#!/usr/bin/env python3
"""
mine_session_history.py
Walks every local Claude Code session transcript for this project
(~/.claude/projects/-Users-namansoni-infinte/*.jsonl) and extracts the real
human<->assistant exchanges: what was asked, what was decided, and why -
the layer of reasoning that git commit messages never capture.

Each transcript line is one event (user turn, assistant turn, tool_use,
tool_result, attachment, bookkeeping). Most "user"-typed events are actually
tool results being fed back to the model, not human text - those, and any
sidechain (subagent-internal) events, are skipped. What's kept is paired into
turns: one real user message plus the assistant's own narrated text response
that followed it (tool calls themselves are not captured - only the surrounding
reasoning/decisions), truncated to keep the corpus from being dominated by a
handful of very long sessions.
"""

import json
import os
from pathlib import Path

SESSIONS_DIR = Path(os.path.expanduser("~/.claude/projects/-Users-namansoni-infinte"))
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OUTPUT_FILE = OUTPUT_DIR / "session_history_corpus.json"

MAX_USER_CHARS = 4000
MAX_ASSISTANT_CHARS = 6000
MIN_USER_CHARS = 4

# Skill/tool bodies get injected into the transcript as literal user-role text
# (indistinguishable at the JSON level from something the human typed) - these
# prefixes mark harness scaffolding rather than human intent, so they're
# dropped instead of polluting the corpus with skill instruction dumps.
BOILERPLATE_PREFIXES = (
    "Base directory for this skill:",
    "<command-name>",
)


def extract_user_text(message):
    """Returns real typed human text, or None if this 'user' event is
    actually a tool_result / non-text payload rather than something the
    human typed."""
    content = message.get("content")
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        if not content:
            return None
        if all(block.get("type") == "text" for block in content):
            return "\n".join(block.get("text", "") for block in content)
    return None


def extract_assistant_text(message):
    """Concatenates only the narrated 'text' blocks of an assistant turn -
    tool_use/thinking blocks are dropped, they're noise for this corpus."""
    content = message.get("content")
    if not isinstance(content, list):
        return ""
    parts = [b.get("text", "") for b in content if b.get("type") == "text"]
    return "\n".join(p for p in parts if p.strip())


def mine_session(path):
    turns = []
    pending_user = None
    pending_meta = None
    assistant_parts = []

    def flush():
        if pending_user is None:
            return
        user_text = pending_user.strip()
        assistant_text = "\n".join(p for p in assistant_parts if p.strip()).strip()
        if len(user_text) < MIN_USER_CHARS:
            return
        if user_text.startswith(BOILERPLATE_PREFIXES):
            return
        turns.append({
            "session_id": path.stem,
            "timestamp": pending_meta.get("timestamp", ""),
            "cwd": pending_meta.get("cwd", ""),
            "git_branch": pending_meta.get("gitBranch", ""),
            "user_text": user_text[:MAX_USER_CHARS],
            "assistant_text": assistant_text[:MAX_ASSISTANT_CHARS],
        })

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                evt = json.loads(line)
            except json.JSONDecodeError:
                continue

            if evt.get("isSidechain"):
                continue

            etype = evt.get("type")
            if etype == "user":
                text = extract_user_text(evt.get("message", {}))
                if text is None:
                    continue
                flush()
                pending_user = text
                pending_meta = evt
                assistant_parts = []
            elif etype == "assistant" and pending_user is not None:
                assistant_parts.append(extract_assistant_text(evt.get("message", {})))

    flush()
    return turns


def mine_all_sessions():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    if not SESSIONS_DIR.exists():
        print(f"No session directory found at {SESSIONS_DIR}; writing empty corpus.")
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump([], f)
        return

    session_files = sorted(SESSIONS_DIR.glob("*.jsonl"))
    print(f"Mining {len(session_files)} session transcripts from: {SESSIONS_DIR}")

    all_turns = []
    for i, path in enumerate(session_files):
        try:
            all_turns.extend(mine_session(path))
        except Exception as e:
            print(f"  skipped {path.name}: {e}")
        if (i + 1) % 50 == 0:
            print(f"  ...{i + 1}/{len(session_files)} sessions parsed, {len(all_turns)} turns so far")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(all_turns, f, indent=2)

    print(f"Successfully extracted {len(all_turns)} human<->assistant turns into {OUTPUT_FILE}")


if __name__ == "__main__":
    mine_all_sessions()
