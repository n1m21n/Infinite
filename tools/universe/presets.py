#!/usr/bin/env python3
"""Index every built-in preset of the language nodes by parsing the C++ tables (no engine rebuild needed).
    presets.py [--src <repo>/src] > presets.json
Output: {"Field Pixel": [{"name","code"}, ...], ...}. The index into each list is the genome's switch slot."""
import json, os, re, sys

NODES = {  # node type -> source file (relative to src/)
    "FieldPixel": "nodes/FieldPixelNode.cpp", "Field Synth": "nodes/FieldSynthNode.cpp", "Field Notes": "nodes/FieldNotesNode.cpp",
    "Field Primitive": "nodes/FieldPrimitiveNode.cpp", "Field Graph": "nodes/FieldGraphNode.cpp", "Field Effect": "nodes/FieldSampleNode.cpp",
    "Sketch": "core/sketch/SketchPresets.h", "Sketch 3D": "core/sketch/Sketch3DPresets.h",
}
ESC = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "r": "\r"}
ENTRY = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*')


def unescape(s):
    return re.sub(r"\\(.)", lambda m: ESC.get(m.group(1), m.group(1)), s)


def code_at(text, i):
    """Read a raw string R"(...)" or a run of adjacent "..." literals starting at i; return (code, end) or None."""
    m = re.compile(r'R"([^(]*)\(').match(text, i)
    if m:
        close = ')' + m.group(1) + '"'
        j = text.index(close, m.end())
        return text[m.end():j], j + len(close)
    lits, j = [], i
    while True:
        m = re.compile(r'\s*"((?:[^"\\]|\\.)*)"').match(text, j)
        if not m:
            break
        lits.append(unescape(m.group(1))); j = m.end()
    return ("".join(lits), j) if lits else None


def parse(path):
    text = open(path, encoding="utf-8").read()
    out, i = [], 0
    while True:
        m = ENTRY.search(text, i)
        if not m:
            break
        got = code_at(text, m.end())
        if got and got[0].strip():
            out.append({"name": unescape(m.group(1)), "code": got[0]}); i = got[1]
        else:
            i = m.end()
    return out


def main():
    src = sys.argv[sys.argv.index("--src") + 1] if "--src" in sys.argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src")
    res = {t: parse(os.path.join(src, f)) for t, f in NODES.items()}
    json.dump(res, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
