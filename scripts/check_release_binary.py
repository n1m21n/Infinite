#!/usr/bin/env python3
"""Refuse to ship a dev build: fail if the binary carries debug-only UI.

Usage: scripts/check_release_binary.py path/to/Infinite.app (or the binary)
The markers are menu strings compiled only when NDEBUG is not defined
(StageMenuBar.cpp, "UI Debugger / Item Picker", "UI Style Editor").
"""
import os
import sys

MARKERS = [b"UI Debugger / Item Picker", b"UI Style Editor"]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    p = sys.argv[1]
    if os.path.isdir(p):
        p = os.path.join(p, "Contents", "MacOS", "Infinite")
    data = open(p, "rb").read()
    found = [m.decode() for m in MARKERS if m in data]
    if found:
        print("NOT A RELEASE BUILD: contains debug-only UI:", ", ".join(found))
        return 1
    print("release binary check: OK (no debug-only UI)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
