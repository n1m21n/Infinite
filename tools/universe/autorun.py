#!/usr/bin/env python3
"""no-llm runner for the patch universe: generate -> gate -> publish (dry by default).

  python3 autorun.py samples --n 40      # sound-only batch, samples page
  python3 autorun.py library --n 40      # picture + audiovisual batch, library
  add --publish to rebuild site data; the push stays a human step.
"""
import argparse, os, random, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BIN = os.environ.get("INFINITE_BIN", os.path.expanduser(
    "~/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite"))


def sh(cmd):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", choices=["samples", "library"])
    ap.add_argument("--n", type=int, default=40)
    ap.add_argument("--workers", type=int, default=max(2, (os.cpu_count() or 4) // 2))
    ap.add_argument("--seed", type=int, default=int(time.time()) % 100000)
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/patch-universe-library/auto"))
    ap.add_argument("--publish", action="store_true")
    a = ap.parse_args()

    kinds = "A" if a.target == "samples" else "V,AV"
    out = os.path.join(a.out, a.target)
    rc = sh([sys.executable, os.path.join(HERE, "styled.py"), "batch", "x",
             "--n", str(a.n), "--seed", str(a.seed), "--kinds", kinds,
             "--out", out, "--bin", BIN, "--workers", str(a.workers)])
    if rc:
        sys.exit(rc)
    if a.publish:
        cat = os.path.join(ROOT, "tools", "catalog", "catalog.py")
        sh([sys.executable, cat, "all", "--lib", out, "--n", "90"])
        sh([sys.executable, os.path.join(HERE, "build_samples_page.py"), "--chrome-only"])
        print("site data rebuilt; review `git status`, then commit and push by hand")


if __name__ == "__main__":
    main()
