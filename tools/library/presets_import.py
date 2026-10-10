"""Pull the app's built-in presets out of the C++ sources so the Library offers them as downloads.
The C++ stays the source of truth; nothing here is hand-copied."""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "r": "\r"}


def _literals(src, i):
    """Concatenate consecutive "..." literals starting at i; return (text, end)."""
    out = []
    while True:
        m = re.compile(r'\s*(?://[^\n]*\n\s*)*"').match(src, i)
        if not m:
            return "".join(out), i
        j = m.end()
        while src[j] != '"':
            if src[j] == "\\":
                out.append(ESC.get(src[j + 1], src[j + 1]))
                j += 2
            else:
                out.append(src[j])
                j += 1
        i = j + 1


def _cstring_presets(path, anchor):
    src = open(os.path.join(ROOT, path)).read()
    src = src[src.index(anchor):]
    res, pos = [], 0
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,', src):
        if m.start() < pos:
            continue
        code, end = _literals(src, m.end())
        if code.strip():
            res.append((m.group(1), code))
            pos = end
    return res


def sketch():
    src = open(os.path.join(ROOT, "src/core/sketch/SketchPresets.h")).read()
    return [(m.group(1), m.group(2)) for m in re.finditer(r'\{"([^"]+)", R"\((.*?)\)"\}', src, re.S)]


def pixel():
    return _cstring_presets("src/nodes/FieldPixelNode.cpp", "FieldPixelNode::Presets()")


def formula():
    return _cstring_presets("src/nodes/FormulaNode.cpp", "kPresets = {")


if __name__ == "__main__":
    for k, f in (("sketch", sketch), ("pixel", pixel), ("formula", formula)):
        p = f()
        print(k, len(p), [n for n, _ in p][:60])
