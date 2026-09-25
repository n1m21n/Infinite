#!/usr/bin/env python3
"""
build_cases.py
Turns this repo's own past fix commits into replay cases (cases.json).

A case is a non-merge commit whose subject starts with "fix" and which modifies or deletes
1..MAX_FILES code files under src/ (files it adds are not targets: nothing before the commit
could point at them). For each case:
  - target_files:   the src/ code files it touched (pre-image paths)
  - target_symbols: the innermost function whose parent-version line range overlaps a
                    changed hunk; a hunk outside any function falls back to the innermost
                    class/struct. Symbol ids use build_ast_graph's naming, so they compare
                    directly with what the brain returns.
  - query_full:     subject + body, trailers dropped and file names redacted to "[file]"
                    (a bug report does not know which file the fix will land in)
  - query_subject:  subject only - the harder variant
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import BlobReader, ParseCache, git, list_commits, CODE_EXTS  # noqa: E402

CASES_FILE = HERE / "cases.json"
MAX_FILES = 10
FIX_RE = re.compile(r"^fix\b", re.IGNORECASE)
TRAILER_RE = re.compile(r"^(Co-Authored-By|Signed-off-by|Reviewed-by):.*$", re.IGNORECASE | re.MULTILINE)
FILE_RE = re.compile(r"(?:[\w.-]+/)*[\w-]+\.(?:cpp|h|hpp|mm|c|cc|py|glsl|metal|md|txt|cmake|sh|json)\b")
HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@", re.MULTILINE)


def changed_old_ranges(parent, commit, path):
    diff = git("diff", "-U0", parent, commit, "--", path).decode("utf-8", "replace")
    ranges = []
    for m in HUNK_RE.finditer(diff):
        start, count = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
        # count 0 = pure insertion after line `start`: it touches whatever encloses start..start+1
        ranges.append((max(start, 1), start + 1) if count == 0 else (start, start + count - 1))
    return ranges


def swallowing_functions(symbols):
    """Functions whose range contains another function definition. Real C++ functions do not;
    these are tree-sitter-cpp error-recovery spans (Objective-C++ in .mm files mostly) and
    would label a whole file region as one symbol."""
    funcs = [(m["line"], m["end_line"], sid) for sid, m in symbols.items()
             if m.get("kind") == "function" and "end_line" in m]
    bad = set()
    for lo, hi, sid in funcs:
        if any(lo <= l2 and h2 <= hi and (l2, h2) != (lo, hi) for l2, h2, _ in funcs):
            bad.add(sid)
    return bad


def innermost(symbols, lo, hi, kinds, skip=()):
    best = None
    for sid, meta in symbols.items():
        if meta.get("kind") not in kinds or "end_line" not in meta or sid in skip:
            continue
        if meta["line"] <= hi and meta["end_line"] >= lo:
            span = meta["end_line"] - meta["line"]
            if best is None or span < best[0]:
                best = (span, sid)
    return best[1] if best else None


def target_symbols(parse, reader, parent, commit, path, blob):
    symbols = parse.get(path, blob, reader)["symbols"]
    skip = swallowing_functions(symbols)
    found = []
    for lo, hi in changed_old_ranges(parent, commit, path):
        sid = (innermost(symbols, lo, hi, ("function",), skip)
               or innermost(symbols, lo, hi, ("class", "struct")))
        if sid and sid not in found:
            found.append(sid)
    return found


def redact(text):
    return FILE_RE.sub("[file]", TRAILER_RE.sub("", text)).strip()


def build():
    reader, parse = BlobReader(), ParseCache()
    cases = []
    for c in list_commits(no_merges=True):
        if not FIX_RE.match(c["subject"]) or not c["parents"]:
            continue
        parent = c["parents"][0]
        status = git("diff-tree", "-r", "--no-commit-id", "--name-status", "-M", parent, c["hash"]).decode()
        targets = []
        for line in status.splitlines():
            parts = line.split("\t")
            kind, old = parts[0][0], parts[1]
            if kind in "MDR" and old.startswith("src/") and old.endswith(CODE_EXTS):
                targets.append(old)
        if not 1 <= len(targets) <= MAX_FILES:
            continue
        syms = []
        for path in targets:
            blob = git("rev-parse", f"{parent}:{path}").decode().strip()
            for s in target_symbols(parse, reader, parent, c["hash"], path, blob):
                if s not in syms:
                    syms.append(s)
        body = redact(c["body"])
        cases.append({
            "commit": c["hash"], "parent": parent, "time": c["time"], "subject": c["subject"],
            "query_full": (c["subject"] + ("\n\n" + body if body else "")).strip(),
            "query_subject": c["subject"],
            "target_files": targets, "target_symbols": syms,
        })
    reader.close()
    cases.sort(key=lambda x: x["time"])
    CASES_FILE.write_text(json.dumps(cases, indent=1))
    n_sym = sum(1 for x in cases if x["target_symbols"])
    print(f"{len(cases)} cases ({n_sym} with target symbols) -> {CASES_FILE}")


if __name__ == "__main__":
    build()
