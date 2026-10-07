"""The application's UI/editor source: src/main.cpp plus every src/app/**/*.cpp.

src/main.cpp was split into translation units under src/app/ (docs/plans/main-split).
Static checkers that used to read src/main.cpp read it through here so they see the
same code wherever it lives, and fail loudly instead of silently finding nothing.
"""
import bisect
import glob
import os
import sys


def root_dir():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sources(root=None):
    root = root or root_dir()
    main = os.path.join(root, "src", "main.cpp")
    if not os.path.isfile(main):
        sys.exit("appsrc: missing " + main)
    app = sorted(glob.glob(os.path.join(root, "src", "app", "**", "*.cpp"), recursive=True))
    if not app:
        sys.exit("appsrc: no translation units under src/app - the split moved or this checker is stale")
    return [main] + app


def read_indexed(root=None):
    """Return (text, locate); locate(line) -> 'relative/file.cpp:line' for a 1-based line of text."""
    root = root or root_dir()
    parts, starts, names = [], [], []
    line = 1
    for p in sources(root):
        t = open(p, encoding="utf-8", errors="replace").read()
        if not t.endswith("\n"):
            t += "\n"
        starts.append(line)
        names.append(os.path.relpath(p, root))
        parts.append(t)
        line += t.count("\n")

    def locate(ln):
        i = bisect.bisect_right(starts, ln) - 1
        return "%s:%d" % (names[i], ln - starts[i] + 1)

    return "".join(parts), locate


def read(root=None):
    return read_indexed(root)[0]
