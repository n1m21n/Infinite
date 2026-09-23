#!/usr/bin/env python3
"""
mine_antigravity_history.py
Walks every local Antigravity (Gemini) agent transcript
(~/.gemini/antigravity/brain/<conversation-id>/.system_generated/logs/transcript.jsonl)
and extracts the same shape of human<->assistant turn that mine_session_history.py
extracts from Claude Code transcripts, so both tools' history can be analyzed together.

Antigravity's transcript format differs from Claude Code's:
- Each line is one step: {"step_index", "source", "type", "status", "created_at", "content"?,
  "tool_calls"?, "thinking"?}.
- A real human message is `type == "USER_INPUT"` (source "USER_EXPLICIT"), wrapped in a
  <USER_REQUEST>...</USER_REQUEST> tag plus harness metadata blocks that aren't part of what
  the user actually typed.
- The model's narrated explanation (the equivalent of Claude's assistant text blocks) is
  `type == "PLANNER_RESPONSE"` entries that carry a non-empty "content" field. Most
  PLANNER_RESPONSE entries carry only "tool_calls" with no content - those are pure tool
  invocations (find_by_name, run_command, ...) and are skipped, mirroring how the Claude
  miner drops tool_use/tool_result blocks and keeps only narrated text.
- `type == "GENERIC"` entries are tool *output* being fed back to the model (command output,
  search results) - skipped for the same reason Claude tool_results are skipped.

Since one Antigravity conversation is often a single long autonomous run (occasionally just
4 human turns across 4000+ transcript lines), a "turn" here is: one USER_INPUT plus every
PLANNER_RESPONSE.content that follows it, up to the next USER_INPUT.

Conversations are only kept if they mention this project's absolute path anywhere in the
transcript (Antigravity is not scoped to one repo the way a Claude Code project directory is).
"""

import json
import os
import re
from pathlib import Path

BRAIN_DIR = Path(os.path.expanduser("~/.gemini/antigravity/brain"))
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OUTPUT_FILE = OUTPUT_DIR / "antigravity_history_corpus.json"

PROJECT_PATH_MARKER = "/Users/namansoni/infinte"

MAX_USER_CHARS = 4000
MAX_ASSISTANT_CHARS = 6000
MIN_USER_CHARS = 4

USER_REQUEST_RE = re.compile(r"<USER_REQUEST>\s*(.*?)\s*</USER_REQUEST>", re.DOTALL)


def extract_user_text(raw_content):
    if not raw_content:
        return None
    match = USER_REQUEST_RE.search(raw_content)
    text = match.group(1) if match else raw_content
    text = text.strip()
    return text or None


def mine_conversation(conv_dir):
    transcript_path = conv_dir / ".system_generated" / "logs" / "transcript.jsonl"
    if not transcript_path.exists():
        return []

    lines_raw = transcript_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not any(PROJECT_PATH_MARKER in line for line in lines_raw):
        return []

    turns = []
    pending_user = None
    pending_timestamp = ""
    assistant_parts = []

    def flush():
        if pending_user is None:
            return
        user_text = pending_user.strip()
        assistant_text = "\n".join(p for p in assistant_parts if p.strip()).strip()
        if len(user_text) < MIN_USER_CHARS:
            return
        turns.append({
            "session_id": conv_dir.name,
            "timestamp": pending_timestamp,
            "cwd": PROJECT_PATH_MARKER,
            "git_branch": "",
            "source_tool": "antigravity",
            "user_text": user_text[:MAX_USER_CHARS],
            "assistant_text": assistant_text[:MAX_ASSISTANT_CHARS],
        })

    for line in lines_raw:
        line = line.strip()
        if not line:
            continue
        try:
            evt = json.loads(line)
        except json.JSONDecodeError:
            continue

        etype = evt.get("type")
        if etype == "USER_INPUT":
            text = extract_user_text(evt.get("content", ""))
            if text is None:
                continue
            flush()
            pending_user = text
            pending_timestamp = evt.get("created_at", "")
            assistant_parts = []
        elif etype == "PLANNER_RESPONSE" and pending_user is not None:
            content = evt.get("content", "")
            if content and content.strip():
                assistant_parts.append(content.strip())

    flush()
    return turns


def mine_all_conversations():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    if not BRAIN_DIR.exists():
        print(f"No Antigravity brain directory found at {BRAIN_DIR}; writing empty corpus.")
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump([], f)
        return

    conv_dirs = sorted(p for p in BRAIN_DIR.iterdir() if p.is_dir())
    print(f"Scanning {len(conv_dirs)} Antigravity conversations from: {BRAIN_DIR}")

    all_turns = []
    kept_conversations = 0
    for i, conv_dir in enumerate(conv_dirs):
        try:
            turns = mine_conversation(conv_dir)
            if turns:
                kept_conversations += 1
                all_turns.extend(turns)
        except Exception as e:
            print(f"  skipped {conv_dir.name}: {e}")
        if (i + 1) % 50 == 0:
            print(f"  ...{i + 1}/{len(conv_dirs)} conversations scanned, {len(all_turns)} turns so far")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(all_turns, f, indent=2)

    print(f"Kept {kept_conversations}/{len(conv_dirs)} conversations that mention this project")
    print(f"Successfully extracted {len(all_turns)} human<->assistant turns into {OUTPUT_FILE}")


if __name__ == "__main__":
    mine_all_conversations()
