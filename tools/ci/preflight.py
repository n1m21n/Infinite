#!/usr/bin/env python3
"""Run on the Mac what CI will run, before anything is pushed.

  tools/ci/preflight.py                      static checks + CI mirror of the macOS and ratchet jobs
  tools/ci/preflight.py --bin PATH           binary the mirrored test steps run against
                                             (default build/Infinite.app/Contents/MacOS/Infinite)
  tools/ci/preflight.py --fast               static checks only (seconds, no binary needed)
  tools/ci/preflight.py --with-slow          also the slow informational CI steps (CLI smoke, ~17 min)
  tools/ci/preflight.py --only drift,i18n    run named checks (see --list)
  tools/ci/preflight.py --list               list checks and the CI steps the mirror would run
  tools/ci/preflight.py --release-app APP    also refuse a debug build (the app you will ship)
  tools/ci/preflight.py --self-test          prove the drift checks catch what they claim to

Two kinds of check:
  * STATIC checks find drift: a source of truth and its copy disagreeing (driver test lists vs
    the tests in src/, baselines vs the driver, translations vs strings, workflows that do not
    parse). They are generic, so a new test or string is covered with no edit here.
  * MIRROR reads .github/workflows/build.yml and runs the `run:` steps of the jobs that can run
    on a Mac (design-ratchet, macos), skipping configure/build. A step added to CI is picked
    up automatically, so preflight cannot fall behind CI.

Writes .git/preflight-stamp (HEAD sha + result) so scripts can tell a pushed commit was checked.
Exit: 0 everything passed, 1 a check failed.
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DRIVER = ".claude/skills/run-infinite-hygiene/driver.sh"
KNOWN = ".claude/skills/run-infinite-hygiene/known-test-failures.txt"
HEADLESS = ".github/scripts/headless-tests.sh"
MAC_BIN = "build/Infinite.app/Contents/MacOS/Infinite"
MIRROR_JOBS = ("design-ratchet", "macos")
SLOW_STEP = re.compile(r"smoke", re.I)  # informational on CI, ~17 min locally: opt in with --with-slow
SKIP_STEP = re.compile(r"cmake\s+(-S|--build)|brew |apt-get|pip install|upload-artifact|ccache", re.I)


def rd(rel):
    with open(os.path.join(ROOT, rel), errors="replace") as f:
        return f.read()


_src_cache = None


def src_text():
    global _src_cache
    if _src_cache is None:
        chunks = []
        for base, _, files in os.walk(os.path.join(ROOT, "src")):
            for fn in files:
                if fn.endswith((".cpp", ".h", ".mm", ".hpp", ".cc")):
                    with open(os.path.join(base, fn), errors="replace") as f:
                        chunks.append(f.read())
        _src_cache = "\n".join(chunks)
    return _src_cache


def sh(cmd, timeout=1800):
    r = subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    return r.returncode, (r.stdout + r.stderr)


# ---------------------------------------------------------------- static checks
def driver_names(text):
    """TEST names the driver runs: entries like "NAME:35" in its group arrays."""
    return sorted(set(re.findall(r'"([A-Z][A-Z0-9_]{3,}):\d+"', text)))


def check_drift(driver_text=None, headless_text=None, known_text=None, source=None):
    """Every test the driver or CI script names must exist in src/; baselines must name driver tests."""
    driver_text = rd(DRIVER) if driver_text is None else driver_text
    headless_text = rd(HEADLESS) if headless_text is None else headless_text
    known_text = rd(KNOWN) if known_text is None else known_text
    source = src_text() if source is None else source
    bad = []
    names = driver_names(driver_text)
    for n in names:
        if f"INFINITE_{n}" not in source:
            bad.append(f"driver runs {n} but src/ has no INFINITE_{n}")
    for var in sorted(set(re.findall(r"check_verdict (INFINITE_[A-Z0-9_]+)", headless_text)
                          + re.findall(r'"== (INFINITE_[A-Z0-9_]+)', headless_text)
                          + re.findall(r"\b(INFINITE_[A-Z0-9_]+TEST)=1", headless_text))):
        if var not in source:
            bad.append(f"headless-tests.sh runs {var} but src/ never reads it")
    for line in known_text.splitlines():
        m = re.match(r"^([A-Z][A-Z0-9_]+)\|", line)
        if m and m.group(1) not in names:
            bad.append(f"known-test-failures.txt lists {m.group(1)} which the driver no longer runs")
    return bad


def check_workflows():
    import yaml
    bad = []
    wf = os.path.join(ROOT, ".github", "workflows")
    for fn in sorted(os.listdir(wf)):
        if fn.endswith((".yml", ".yaml")):
            try:
                yaml.safe_load(open(os.path.join(wf, fn)))
            except Exception as e:
                bad.append(f"{fn}: {str(e).splitlines()[0]}")
    return bad


def check_i18n():
    bad = []
    rc, out = sh("python3 tools/i18n/lint.py")
    if rc != 0 or re.search(r"[1-9]\d* unwrapped", out):
        bad.append("i18n lint: " + out.strip().splitlines()[-1][:200])
    rc, out = sh("python3 tools/i18n/extract.py report")
    for ln in out.splitlines():
        m = re.search(r"(\w+): .*?(\d+) missing.*?(\d+) format mismatches", ln)
        if m and (int(m.group(2)) or int(m.group(3))):
            bad.append("i18n " + ln.strip())
    return bad


def check_manuals():
    """Brand lint on the committed manual PDFs (em dashes, fonts, page size)."""
    pdfs = [p for p in ("Infinite_Node_Reference_Manual.pdf", "Field_Language_Manual.pdf",
                        "docs/headless/Infinite_Headless_User_Guide.pdf") if os.path.exists(os.path.join(ROOT, p))]
    if not pdfs or subprocess.run("command -v pdffonts", shell=True, capture_output=True).returncode != 0:
        return None  # nothing to check / poppler missing: skipped, not failed
    rc, out = sh("python3 tools/docs-shots/pdf_check.py " + " ".join(pdfs))
    return [] if rc == 0 else [l for l in out.splitlines() if l.strip()][-6:]


def check_release_app(app):
    rc, out = sh(f'python3 scripts/check_release_binary.py "{app}"')
    return [] if rc == 0 else [out.strip()]


STATIC = {
    "drift": ("driver/CI test lists, baselines and src/ agree", check_drift),
    "workflows": ("every workflow file parses", check_workflows),
    "i18n": ("translations complete, no format mismatches", check_i18n),
    "manuals": ("committed manual PDFs pass brand lint", check_manuals),
}


# ---------------------------------------------------------------- CI mirror
def mirror_steps(binary, with_slow=False):
    import yaml
    app = binary.split("/Contents/MacOS/")[0] if "/Contents/MacOS/" in binary else binary
    wf = yaml.safe_load(rd(".github/workflows/build.yml"))
    steps = []
    for job in MIRROR_JOBS:
        for st in wf["jobs"][job]["steps"]:
            cmd = st.get("run")
            if not cmd or SKIP_STEP.search(cmd):
                continue
            name = st.get("name", cmd.strip().splitlines()[0][:60])
            if SLOW_STEP.search(name) and not with_slow:
                continue
            cmd = cmd.replace(MAC_BIN, binary).replace("build/Infinite.app", app)
            # A dev build correctly fails the release-binary check; it only proves something on a release app.
            soft = bool(st.get("continue-on-error")) or ("check_release_binary" in cmd and "build-dev" in binary)
            steps.append((job, name, cmd, soft))
    return steps


def run_mirror(binary, results, with_slow=False):
    steps = mirror_steps(binary, with_slow)
    app = binary.split("/Contents/MacOS/")[0] if "/Contents/MacOS/" in binary else binary
    uses = lambda cmd: binary in cmd or app in cmd
    have = os.path.exists(os.path.join(ROOT, binary))
    if any(uses(s[2]) for s in steps) and not have:
        results.append(("mirror", "SKIP", 0, f"no binary at {binary} (build it, or pass --bin); ratchet-only steps still run"))
    for job, name, cmd, soft in steps:
        if uses(cmd) and not have:
            continue
        t = time.time()
        rc, out = sh(cmd)
        status = "PASS" if rc == 0 else ("INFO" if soft else "FAIL")
        results.append((f"{job}: {name}", status, time.time() - t, "" if rc == 0 else out.strip()[-600:]))


# ---------------------------------------------------------------- self-test
def self_test():
    ok = True

    def expect(label, bad, want_substr):
        nonlocal ok
        hit = any(want_substr in b for b in bad)
        print(f"  {'ok  ' if hit else 'FAIL'} {label}")
        ok &= hit

    src = "INFINITE_REALTEST getenv"
    driver = 'GROUP=(\n  "REALTEST:1"\n  "BLOCKSETTEST:1"\n)\n'
    expect("deleted test still listed in the driver", check_drift(driver, "", "", src), "BLOCKSETTEST")
    expect("baseline names a test the driver dropped", check_drift(driver, "", "GONETEST|why\n", src), "GONETEST")
    expect("headless script runs a var src never reads",
           check_drift(driver, "check_verdict INFINITE_NOPETEST \"OK\"\n", "", src), "INFINITE_NOPETEST")
    clean = check_drift('GROUP=(\n  "REALTEST:1"\n)\n', "", "REALTEST|why\n", src)
    print(f"  {'ok  ' if not clean else 'FAIL'} a consistent set passes")
    ok &= not clean
    return 0 if ok else 1


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=MAC_BIN)
    ap.add_argument("--fast", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--with-slow", action="store_true", help="include the slow informational CI steps (CLI smoke)")
    ap.add_argument("--release-app")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.list:
        for k, (d, _) in STATIC.items():
            print(f"static  {k:10s} {d}")
        for job, name, cmd, soft in mirror_steps(a.bin, a.with_slow):
            print(f"mirror  {job:15s} {name}{'  (info)' if soft else ''}")
        return 0
    only = set(a.only.split(",")) if a.only else None
    results = []
    for key, (desc, fn) in STATIC.items():
        if only and key not in only:
            continue
        t = time.time()
        bad = fn()
        if bad is None:
            results.append((key, "SKIP", time.time() - t, "tool missing"))
        else:
            results.append((key, "PASS" if not bad else "FAIL", time.time() - t, "\n".join(bad)))
    if a.release_app:
        bad = check_release_app(a.release_app)
        results.append(("release-app", "PASS" if not bad else "FAIL", 0, "\n".join(bad)))
    if not a.fast and (not only or "mirror" in only):
        run_mirror(a.bin, results, a.with_slow)
    failed = [r for r in results if r[1] == "FAIL"]
    print()
    for name, status, secs, detail in results:
        print(f"{status:5s} {secs:6.1f}s  {name}")
        if status in ("FAIL", "SKIP") and detail:
            for ln in detail.splitlines()[:8]:
                print("          " + ln[:200])
    print(f"\npreflight: {'FAILED' if failed else 'ok'} ({len(failed)} failing of {len(results)})")
    try:
        head = subprocess.run("git rev-parse HEAD", shell=True, cwd=ROOT, capture_output=True, text=True).stdout.strip()
        gitdir = subprocess.run("git rev-parse --git-dir", shell=True, cwd=ROOT, capture_output=True, text=True).stdout.strip()
        with open(os.path.join(ROOT, gitdir, "preflight-stamp"), "w") as f:
            f.write(f"{head} {'FAIL' if failed else 'OK'} {'fast' if a.fast else 'full'}\n")
    except OSError:
        pass
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
