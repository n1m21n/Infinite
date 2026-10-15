#!/usr/bin/env python3
"""One command: capture every node, panel, dialog and dropdown from a built Infinite.app, frame the shots to the
Brand v2 Paper standard, drop them into the Node Reference Manual, the Field Language Manual and the Headless User Guide,
and rebuild the three PDFs.

    python3 tools/docs-shots/run.py --app ~/Desktop/Infinite-3.5.app            # full release refresh
    python3 tools/docs-shots/run.py --limit 5 --manuals node                    # smoke run (PDFs go to docs/shots/out/)
    python3 tools/docs-shots/run.py --no-capture                                # rebuild PDFs from the existing shots

A full run shoots ~330 nodes x 2 states (about 1.7 s each) plus panels, dialogs and dropdowns, then builds. It refuses to
start while a cmake/make/clang build is running. Subset runs (--limit, --nodes, --category) never overwrite the shipped PDFs.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import capture  # noqa: E402
import common as C  # noqa: E402

PY = sys.executable


def sh(cmd):
    print("+", " ".join(os.path.relpath(c, C.ROOT) if c.startswith(C.ROOT) else c for c in cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=C.ROOT)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default=C.DEFAULT_APP, help="Infinite.app to shoot (default $INFINITE_APP or the 3.5 build)")
    ap.add_argument("--theme", default="dark", choices=["dark", "light"])
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--nodes", help="'Category|Name,Category|Name'")
    ap.add_argument("--category", action="append")
    ap.add_argument("--no-dropdowns", action="store_true")
    ap.add_argument("--no-scenes", action="store_true", help="skip panels, windows, dialogs and the UI sheet")
    ap.add_argument("--no-capture", action="store_true")
    ap.add_argument("--manuals", default="node,field,headless", help="comma list of node, field, headless, or none")
    a = ap.parse_args()

    subset = bool(a.limit or a.nodes or a.category)
    if not a.no_capture:
        cmd = ["--app", a.app, "--theme", a.theme]
        if a.limit: cmd += ["--limit", str(a.limit)]
        if a.nodes: cmd += ["--nodes", a.nodes]
        for c in a.category or []: cmd += ["--category", c]
        if not a.no_dropdowns and not subset: cmd += ["--dropdowns"]
        if not a.no_scenes and not subset: cmd += ["--scenes", "all,ui"]
        if subset and not a.no_scenes: cmd += ["--scenes", "panel/modmatrix,dialog/about"]
        if capture.main(cmd):
            sys.exit("capture failed")
    todo = [m for m in a.manuals.split(",") if m and m != "none"]
    out = os.path.join(C.SHOTS, "out")
    os.makedirs(out, exist_ok=True)
    if "node" in todo:
        cmd = [PY, "tools/manual/build.py"]
        if subset: cmd += ["--out", os.path.join(out, "Node_Reference_Manual.pdf"), "--no-base", "--atlas-limit", "5"]
        sh(cmd)
    if "field" in todo:
        cmd = [PY, "tools/manual/field/build_addendum.py"]
        if subset: cmd += ["--out", os.path.join(out, "Field_Language_Manual.pdf"), "--no-base"]
        sh(cmd)
    if "headless" in todo:
        sh([PY, "tools/headless-docs/build.py", "--app", a.app] + (["--out", os.path.join(out, "Headless_User_Guide.pdf")] if subset else []))
        if not subset:   # the guide's shipped website copy
            sh(["cp", os.path.join(C.ROOT, "docs/headless/Infinite_Headless_User_Guide.pdf"), os.path.join(C.ROOT, "website/assets/Infinite_Headless_User_Guide.pdf")])
    print("done.", "PDFs in docs/shots/out/ (subset run)" if subset else "shipped PDFs updated; review with git status")


if __name__ == "__main__":
    main()
