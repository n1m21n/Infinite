#!/usr/bin/env python3
"""Rewrite free-text `// ui-audit-ok: <reason>` tags to the word form ui_audit.py accepts (owner 2026-11-24):

    // ui-audit-ok: byte / buffer arithmetic, not a screen quantity  ->  // ui-audit-ok: bytes (byte / buffer arithmetic, ...)

The first rule whose pattern matches the old reason picks the word; the old reason stays as the note. A reason no rule
matches is printed and left (ui_audit reports it as bad_ok).

    python3 tools/design/ok_words.py [--dry]
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402

RULES = [
    ("core", r"core cannot"),
    ("hash", r"\bhash"),
    ("mask", r"channel mask|bit packing|nearest-filter flag|flag bits?"),
    ("rpc", r"\brpc\b"),
    ("patch_data", r"patch data|demo patch|canvas placement|canvas spawn|demo segment|demo cell|demo clip"),
    ("bytes", r"\bbytes?\b|buffer|string length|ascii"),
    ("log", r"\blog\b|file name|folder name"),
    ("sentinel", r"sentinel"),
    ("key", r"\bkey\b|numpad|shift speeds"),
    ("unit", r"\bdB\b|radians|degrees|percent to|0-255"),
    ("index", r"\bindex\b|\bid\b|stride|axis index|param id|id layout"),
    ("world", r"world-space"),
    ("timing", r"polling|pacing|pacer|smoothing|budget|throttle|delay|wait|frames?\b|clock|tempo estimation|tick"),
    ("model", r"."),   # everything else names a model value, default, count, range or mode
]
TAG = re.compile(r"(//\s*ui-audit-ok:?\s*)(.*?)\s*$")


def word_of(reason):
    for w, pat in RULES:
        if re.search(pat, reason, re.I): return w
    return None


def main():
    dry = "--dry" in sys.argv
    changed = 0
    for rel, src in ui_audit.ui_files():
        lines = src.split("\n")
        hit = False
        for n, l in enumerate(lines):
            m = TAG.search(l)
            if not m: continue
            reason = m.group(2)
            first = re.match(r"[A-Za-z_]*", reason).group(0)
            if first in ui_audit.OK_WORDS: continue
            w = word_of(reason)
            if not w: print(f"left {rel}:{n + 1} {reason}"); continue
            note = reason.strip()
            lines[n] = l[:m.start()] + m.group(1).rstrip() + " " + w + (f" ({note})" if note else "")
            print(f"{rel}:{n + 1} {w} <- {note}")
            hit = True; changed += 1
        if hit and not dry: (ui_audit.ROOT / rel).write_text("\n".join(lines), encoding="utf-8")
    print(f"ok_words: {changed} tags rewritten")


if __name__ == "__main__":
    main()
