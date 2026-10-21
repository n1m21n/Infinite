#!/usr/bin/env python3
"""Live view of a dispatched Claude session on the other laptop.

`claude -p` prints nothing until it finishes, but it writes its full transcript as it goes. This
tails that transcript over SSH and prints every message, tool call and result as it happens.

    python3 scripts/dispatch_watch.py <slug>            # follow live (Ctrl-C to stop)
    python3 scripts/dispatch_watch.py <slug> --replay   # print everything so far, then follow
    python3 scripts/dispatch_watch.py <slug> --full     # (raw view) do not truncate tool results
    python3 scripts/dispatch_watch.py <slug> --raw      # every tool call and result, as it is

Default view is plain language: what it is reading, writing, running and committing, and what it says.
"""
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone

AIR = "namansoni@Namans-MacBook-Air.local"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes", "-o", "ConnectTimeout=5",
       "-o", "ServerAliveInterval=15", "-i", "~/.ssh/id_ed25519".replace("~", __import__("os").path.expanduser("~"))]

C = {"dim": "\033[2m", "bold": "\033[1m", "cyan": "\033[36m", "green": "\033[32m",
     "yellow": "\033[33m", "red": "\033[31m", "off": "\033[0m"}


def clip(s, n, full):
    s = s if isinstance(s, str) else json.dumps(s, ensure_ascii=False)
    return s if full or len(s) <= n else s[:n] + f"{C['dim']} ...[{len(s) - n} more chars]{C['off']}"


def show(line, full):
    try:
        rec = json.loads(line)
    except ValueError:
        return
    t = rec.get("type")
    ts = (rec.get("timestamp") or "")[11:19]
    msg = rec.get("message") or {}
    content = msg.get("content")
    if isinstance(content, str):
        if t == "user":
            print(f"{C['dim']}{ts}{C['off']} {C['yellow']}PROMPT{C['off']} {clip(content, 400, full)}")
        return
    for b in content or []:
        k = b.get("type")
        if k == "text" and t == "assistant":
            print(f"\n{C['dim']}{ts}{C['off']} {C['bold']}CLAUDE{C['off']} {b['text'].strip()}")
        elif k == "thinking":
            print(f"{C['dim']}{ts} (thinking) {clip(b.get('thinking', ''), 300, full)}{C['off']}")
        elif k == "tool_use":
            inp = b.get("input", {})
            brief = inp.get("command") or inp.get("file_path") or inp.get("pattern") or inp.get("path") or inp
            extra = ""
            if b.get("name") in ("Write", "Edit") and "content" in inp:
                extra = f" ({len(inp['content'])} chars)"
            print(f"{C['dim']}{ts}{C['off']} {C['cyan']}TOOL {b.get('name')}{C['off']} {clip(brief, 300, full)}{extra}")
        elif k == "tool_result":
            body = b.get("content")
            if isinstance(body, list):
                body = "\n".join(x.get("text", "") for x in body if isinstance(x, dict))
            col = C["red"] if b.get("is_error") else C["green"]
            print(f"{C['dim']}{ts}{C['off']} {col}RESULT{C['off']} {clip((body or '').strip(), 600, full)}")


WT = "/infinte-wt/"
NAMES = {}  # tool_use id -> (name, input)


def rel(path):
    path = path or ""
    return path.split(WT, 1)[1].split("/", 1)[-1] if WT in path else path.replace(os.path.expanduser("~") + "/", "")


def local(ts):
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone().strftime("%H:%M")
    except ValueError:
        return ""


def describe_bash(inp):
    d, cmd = inp.get("description"), (inp.get("command") or "").strip()
    m = re.search(r"git commit .*?-m\s+(?:\"|')?(?:\$\(cat <<'?EOF'?\s*)?([^\n\"']+)", cmd)
    if "git commit" in cmd and m:
        return f"Saving progress (git commit): {m.group(1).strip()[:100]}"
    if d:
        return d
    m = re.search(r"python3\s+(?:-I\s+)?(\S+\.py)", cmd)
    if m:
        return f"Running {m.group(1).split('/')[-1]}"
    if cmd.startswith("git "):
        return f"Checking git ({' '.join(cmd.split()[:2])})"
    first = cmd.split()[0] if cmd else ""
    if first in ("ls", "cat", "head", "tail", "wc", "find", "grep", "sed", "file", "stat", "diff"):
        return "Looking through the repo files"
    if "python3 -c" in cmd or "python3 -" in cmd:
        return "Running a quick python check"
    if first in ("mkdir", "cp", "mv", "chmod", "rm"):
        return "Organising files"
    return "Running a shell command"


def show_plain(line):
    try:
        rec = json.loads(line)
    except ValueError:
        return
    t, msg = rec.get("type"), rec.get("message") or {}
    content = msg.get("content")
    ts = local(rec.get("timestamp") or "")
    if isinstance(content, str):
        return
    for b in content or []:
        k = b.get("type")
        if k == "text" and t == "assistant" and b["text"].strip():
            print(f"\n{C['dim']}{ts}{C['off']} {C['bold']}Claude says:{C['off']} {b['text'].strip()}")
        elif k == "tool_use":
            name, inp = b.get("name"), b.get("input", {})
            NAMES[b.get("id")] = name
            if name == "Read":
                what = f"Reading {rel(inp.get('file_path'))}"
            elif name == "Write":
                n = inp.get("content", "").count("\n") + 1
                what = f"Writing a new file {rel(inp.get('file_path'))} ({n} lines)"
            elif name == "Edit":
                what = f"Editing {rel(inp.get('file_path'))}"
            elif name in ("Glob", "Grep"):
                what = f"Searching the repo for {inp.get('pattern', '')}"
            elif name == "Bash":
                what = describe_bash(inp)
            elif name == "Skill":
                what = f"Loading the skill {inp.get('skill', '')}"
            else:
                what = name
            print(f"{C['dim']}{ts}{C['off']} {C['cyan']}-{C['off']} {what}")
        elif k == "tool_result":
            body = b.get("content")
            if isinstance(body, list):
                body = "\n".join(x.get("text", "") for x in body if isinstance(x, dict))
            body = (body or "").strip()
            if b.get("is_error"):
                first = body.splitlines()[0][:140] if body else "failed"
                print(f"    {C['red']}failed:{C['off']} {first}")
            elif NAMES.get(b.get("tool_use_id")) == "Bash" and body:
                m = re.search(r'"ok":\s*(true|false)', body)
                if m:
                    print(f"    {C['green'] if m.group(1) == 'true' else C['red']}result: {'ok' if m.group(1) == 'true' else 'not ok'}{C['off']}")
                elif re.search(r"\b(FAIL|Traceback|Error)\b", body):
                    print(f"    {C['red']}problem:{C['off']} {body.splitlines()[-1][:140]}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    slug, replay, full = sys.argv[1], "--replay" in sys.argv, "--full" in sys.argv
    raw = "--raw" in sys.argv or full
    proj = f"$HOME/.claude/projects/-Users-namansoni-infinte-wt-{slug}"
    start = "+1" if replay else "0"
    remote = f'f=$(ls -t {proj}/*.jsonl 2>/dev/null | head -1); [ -n "$f" ] || {{ echo no-transcript-yet >&2; exit 1; }}; tail -n {start} -F "$f"'
    if not replay:
        remote = remote.replace("-n 0", "-n 5")
    p = subprocess.Popen(SSH + [AIR, remote], stdout=subprocess.PIPE, text=True, bufsize=1)
    try:
        for line in p.stdout:
            show(line, full) if raw else show_plain(line)
            sys.stdout.flush()
    except KeyboardInterrupt:
        p.terminate()


if __name__ == "__main__":
    main()
