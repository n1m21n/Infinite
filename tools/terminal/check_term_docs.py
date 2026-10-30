#!/usr/bin/env python3
"""Every call named in the Sketch and Sketch 3D references (src/core/TermDocs.h) is registered by its
engine, and every registered function is documented. Exits 1 and names the drift."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
docs = (root / "src/core/TermDocs.h").read_text()


def section(name):
    return re.search(r"k%s = R\"DOC\((.*?)\)DOC\"" % name, docs, re.S).group(1)


failed = False
for name, engine in (("Sketch", "SketchEngine.cpp"), ("Sketch3D", "Sketch3DEngine.cpp")):
    text = section(name).split("## Wrong and right")[0]
    registered = set(re.findall(r'Reg\(c, g, "(\w+)"', (root / "src/core/sketch" / engine).read_text()))
    named = set(re.findall(r"\b([a-zA-Z]\w*)\(", text)) - {"function", "if", "for", "draw", "code", "sin"}
    for word in sorted(named - registered):
        print(f"{name}: docs name {word}() but {engine} does not register it")
        failed = True
    for word in sorted(registered - named):
        print(f"{name}: {engine} registers {word}() but the docs do not mention it")
        failed = True
print("term docs: drift" if failed else "term docs: ok")
sys.exit(1 if failed else 0)
