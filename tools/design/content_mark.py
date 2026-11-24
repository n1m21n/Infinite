#!/usr/bin/env python3
"""Wrap whole functions in `// ui-content-begin: <reason>` .. `// ui-content-end` (tools/design/kit.py).

    python3 tools/design/content_mark.py SPEC        # SPEC lines: path | FunctionName | reason
    python3 tools/design/content_mark.py SPEC --dry  # print what would be wrapped

Only for drawing that is the picture (a scope, a canvas, a grid), never chrome; content_report.py --check fails on a
widget call inside a region. A function is found by its definition line (a name followed by '(' at indent <= 3 with
no ';' on the line), and its body by brace matching that skips strings, chars and comments. Every definition with
that name in the file is wrapped; an already-wrapped one is left alone.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def body_end(src, open_i):
    """index just after the '}' that closes the '{' at open_i"""
    depth, i, n = 0, open_i, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            i = src.find("\n", i); i = n if i < 0 else i; continue
        if src.startswith("/*", i):
            i = src.find("*/", i) + 2; continue
        if c in "\"'":
            if c == '"' and src[i - 1:i] == "R":   # raw string R"( ... )"
                d = src[i + 1:src.find("(", i)]
                i = src.find(")" + d + '"', i) + len(d) + 2; continue
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            i = j + 1; continue
        if c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return i + 1
        i += 1
    raise ValueError("unbalanced braces")


def wrap(src, func, reason):
    lines_done, out, pos = 0, [], 0
    sig = re.compile(r"^ {0,3}(?!return\b|else\b|if\b)[\w:<>*&, \t]*?\b" + re.escape(func) + r"\s*\([^;{]*$", re.M)
    for m in sig.finditer(src):
        if m.start() < pos: continue
        before = src[:m.start()].rstrip("\n").rsplit("\n", 1)[-1]
        prev2 = src[:m.start()].rstrip("\n").rsplit("\n", 2)
        if "ui-content-begin" in before or (len(prev2) > 1 and "ui-content-begin" in prev2[-2]): continue
        open_i = src.find("{", m.start())
        semi = src.find(";", m.start())
        if open_i < 0 or (0 <= semi < open_i): continue   # a declaration, not a definition
        end = body_end(src, open_i)
        indent = re.match(r" *", src[m.start():]).group(0)
        out.append(src[pos:m.start()])
        out.append(f"{indent}// ui-content-begin: {reason}\n")
        out.append(src[m.start():end])
        out.append(f"\n{indent}// ui-content-end")
        pos = end
        lines_done += 1
    out.append(src[pos:])
    return "".join(out), lines_done


def main():
    spec = pathlib.Path(sys.argv[1]).read_text().splitlines()
    dry = "--dry" in sys.argv
    by_file = {}
    for l in spec:
        if not l.strip() or l.lstrip().startswith("#"): continue
        path, func, reason = (x.strip() for x in l.split("|", 2))
        by_file.setdefault(path, []).append((func, reason))
    bad = 0
    for path, items in by_file.items():
        f = ROOT / path
        src = f.read_text()
        for func, reason in items:
            src, n = wrap(src, func, reason)
            print(f"{path}: {func}: {n} wrapped")
            if n == 0: bad += 1
        if not dry: f.write_text(src)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
