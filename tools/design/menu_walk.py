#!/usr/bin/env python3
"""Run the right-click menu walk (MENUALIVETEST) on the menus a change touched, or on all of them.

Default: the menus whose popup id lives in a file changed since main (committed, staged, unstaged or new). A change to
the walk itself or to the shared menu parts walks everything, as does --all. A changed file that holds no walked menu
(an edit helper the menus call, say) is listed as not walked, so a full walk can follow before a release.
Uses the dev build at build-dev/ (or --app), a clean HOME, and prints the walk's summary and its failed rows.
Plan: internal/plans/ui-system/lint-plan.md (Block E).
"""
import argparse, os, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST = ROOT / "src/app/selftest/MenuAliveTests.cpp"
APP = ROOT / "build-dev/Infinite.app/Contents/MacOS/Infinite"
# Changing one of these can move any menu: walk them all.
SHARED = ("src/app/selftest/MenuAliveTests.cpp", "src/app/ui/design/components/MenuParts")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.split("\n")


def changed_files(base):
    mb = (git("merge-base", base, "HEAD") or [""])[0].strip() or base
    names = set(git("diff", "--name-only", mb)) | set(git("ls-files", "--others", "--exclude-standard"))
    return sorted(n for n in names if n.startswith("src/"))


def walked_roots():
    return sorted(set(re.findall(r'"(##[A-Za-z0-9_]+)"', TEST.read_text())))


def roots_in(path, roots):
    text = (ROOT / path).read_text(errors="ignore") if (ROOT / path).exists() else ""
    return [r for r in roots if f'"{r}"' in text]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true", help="walk every menu")
    ap.add_argument("--base", default="main")
    ap.add_argument("--app", default=str(APP))
    ap.add_argument("--timeout", type=int, default=1500)
    a = ap.parse_args()

    roots = walked_roots()
    pick = None
    if not a.all:
        files = changed_files(a.base)
        if any(f.startswith(SHARED) for f in files):
            print("menu_walk: the walk or the shared menu parts changed: walking every menu")
        else:
            pick, loose = set(), []
            for f in files:
                hit = roots_in(f, roots)
                pick.update(hit)
                if not hit:
                    loose.append(f)
            for f in loose:
                print(f"menu_walk: {f} holds no walked menu (not walked; run --all before a release)")
            if not pick:
                print("menu_walk: no walked menu changed since", a.base)
                return 0
            print("menu_walk: walking", ", ".join(sorted(pick)))

    env = dict(os.environ, INFINITE_MENUALIVETEST="1", HOME=tempfile.mkdtemp(prefix="menuwalk-"))
    if pick is not None:
        env["INFINITE_MENUALIVE_ROOTS"] = ",".join(sorted(pick))
    log = pathlib.Path(tempfile.mkstemp(prefix="menuwalk-", suffix=".log")[1])
    # Under a pty (script -q): redirected output is lost when a timeout kills the run.
    try:
        subprocess.run(["script", "-q", str(log), a.app], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=a.timeout)
    except subprocess.TimeoutExpired:
        print(f"menu_walk: timed out after {a.timeout} s")
    lines = log.read_text(errors="ignore").replace("\r", "").splitlines()
    for line in lines:
        if line.startswith(("MENUALIVE", "  menu with fails")) or "[FAIL]" in line or "[fail]" in line:
            print(line)
    print("menu_walk: log", log)
    return 0 if any(l.startswith("MENUALIVETEST OK") for l in lines) else 1


if __name__ == "__main__":
    sys.exit(main())
