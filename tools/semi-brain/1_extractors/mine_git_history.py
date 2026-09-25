#!/usr/bin/env python3
"""
mine_git_history.py
High-speed batch Git history miner using single-pass git log.
Extracts:
- Commit types (feat, fix, refactor, perf, chore)
- Subsystem scopes (arrange, audio, field, render3d, nodes, ui, platform)
- Commit rationales, bullet points, and root-cause explanations
- File modification stats and diff summaries
"""

import json
import os
import re
import subprocess
from pathlib import Path

REPO_PATH = Path(__file__).resolve().parents[3]
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OUTPUT_FILE = OUTPUT_DIR / "git_commits_corpus.json"

CONVENTIONAL_PATTERN = re.compile(r"^(feat|fix|refactor|perf|test|docs|chore|style|build)(?:\(([^)]+)\))?:\s*(.+)$", re.IGNORECASE)

def parse_commit_message(title, body):
    match = CONVENTIONAL_PATTERN.match(title)
    if match:
        ctype = match.group(1).lower()
        scope = match.group(2).lower() if match.group(2) else "global"
        desc = match.group(3)
    else:
        title_lower = title.lower()
        if title_lower.startswith("fix") or "fix" in title_lower:
            ctype = "fix"
        elif title_lower.startswith("add") or "feat" in title_lower or "implement" in title_lower:
            ctype = "feat"
        elif "refactor" in title_lower or "redesign" in title_lower:
            ctype = "refactor"
        elif "perf" in title_lower or "speed" in title_lower or "optimiz" in title_lower:
            ctype = "perf"
        else:
            ctype = "feat"
            
        if "arrange" in title_lower or "timeline" in title_lower or "clip" in title_lower:
            scope = "arrange"
        elif "audio" in title_lower or "dsp" in title_lower or "vst" in title_lower or "synth" in title_lower:
            scope = "audio"
        elif "field" in title_lower or "expr" in title_lower:
            scope = "field"
        elif "render" in title_lower or "3d" in title_lower or "mesh" in title_lower or "geom" in title_lower:
            scope = "render3d"
        elif "ui" in title_lower or "knob" in title_lower or "panel" in title_lower:
            scope = "ui"
        else:
            scope = "core"
        desc = title

    bullets = []
    body_lines = []
    for line in body.split("\n"):
        line_str = line.strip()
        if not line_str:
            continue
        if line_str.startswith("-") or line_str.startswith("*") or line_str.startswith("•"):
            bullets.append(line_str.lstrip("-*• ").strip())
        else:
            body_lines.append(line_str)

    return {
        "type": ctype,
        "scope": scope,
        "title": title,
        "description": desc,
        "bullets": bullets,
        "body": "\n".join(body_lines),
        "raw": f"{title}\n\n{body}".strip()
    }

LOG_FORMAT = "--pretty=format:===COMMIT_START===%n%H%n%an%n%ae%n%ad%n%s%n===BODY_START===%n%b%n===BODY_END==="


def parse_log_output(stdout):
    """`git log --stat --summary LOG_FORMAT` output -> corpus records, in log order."""
    commits_data = []
    for chunk in stdout.split("===COMMIT_START==="):
        chunk = chunk.strip()
        if not chunk:
            continue

        parts = chunk.split("===BODY_START===")
        header_lines = parts[0].strip().split("\n")
        body_and_stat = parts[1] if len(parts) > 1 else ""

        body_parts = body_and_stat.split("===BODY_END===")
        body = body_parts[0].strip() if len(body_parts) > 0 else ""
        stat = body_parts[1].strip() if len(body_parts) > 1 else ""

        if len(header_lines) >= 4:
            chash = header_lines[0]
            author_name = header_lines[1]
            author_email = header_lines[2]
            author_date = header_lines[3]
            title = header_lines[4] if len(header_lines) > 4 else ""
        else:
            continue

        commits_data.append({
            "hash": chash,
            "author_name": author_name,
            "author_email": author_email,
            "author_date": author_date,
            "parsed_message": parse_commit_message(title, body),
            "stat": stat
        })
    return commits_data


def mine_commits(revs=None):
    """Records for the whole history (revs=None) or just the listed commit hashes."""
    cmd = ["git", "log", "--stat", "--summary", LOG_FORMAT]
    if revs is not None:
        if not revs:
            return []
        cmd = ["git", "log", "--no-walk=unsorted", "--stat", "--summary", LOG_FORMAT, *revs]
    res = subprocess.run(cmd, cwd=REPO_PATH, capture_output=True, text=True, errors="replace", check=True)
    return parse_log_output(res.stdout)


def commit_files(revs=None):
    """{hash: [paths]} each commit touched (merges: the diff against their first parent).
    Every commit in history (revs=None) or just the listed hashes."""
    cmd = ["git", "log", "--diff-merges=first-parent", "--name-only", "--format=%x1e%H"]
    if revs is not None:
        if not revs:
            return {}
        cmd = cmd[:2] + ["--no-walk=unsorted"] + cmd[2:] + list(revs)
    res = subprocess.run(cmd, cwd=REPO_PATH, capture_output=True, text=True, errors="replace", check=True)
    out = {}
    for rec in res.stdout.split("\x1e"):
        lines = [l for l in rec.strip().splitlines() if l.strip()]
        if lines:
            out[lines[0]] = lines[1:]
    return out


def mine_repository():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Mining Git history from: {REPO_PATH}")
    commits_data = mine_commits()

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(commits_data, f, indent=2)

    print(f"Successfully saved {len(commits_data)} commits into {OUTPUT_FILE}")

if __name__ == "__main__":
    mine_repository()
