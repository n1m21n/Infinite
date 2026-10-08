#!/usr/bin/env python3
"""Lint art/icons/src/*-20.svg against the Infinite Glyphs spec (grid, stroke, caps, padding, no transforms)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import glyphlib

def main():
    n = 0
    for f in glyphlib.icons():
        _, bad = glyphlib.outline(f)
        for b in bad: print(f"{f.name}: {b}"); n += 1
    print(f"icon lint: {len(glyphlib.icons())} icons, {n} problems"); return 1 if n else 0

if __name__ == "__main__":
    sys.exit(main())
