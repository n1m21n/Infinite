#!/usr/bin/env python3
"""ibuild: the one way to build Infinite on a small Mac (8 GB, M2).

Why it exists: three sessions each running `make -j10` on 8 GB RAM pushed the
machine into swap (load 32, 9 GB swap) and hung it. This wraps every build in:
  - a machine-wide lock, so builds queue instead of piling up
  - a -j chosen from free RAM / swap / cores, not a constant
  - low priority (nice), so the UI stays responsive
  - a "is this build needed at all?" check (stale object count)
  - per-mode build dirs (dev is arm64-only, half the work of the universal build)

Usage:
  scripts/ibuild.py status                      machine state + recommended -j
  scripts/ibuild.py plan  [--mode dev]          how many files are stale; "up to date" = skip
  scripts/ibuild.py build [--mode dev] [--target Infinite] [--jobs N] [--wait SEC]
  scripts/ibuild.py obj src/foo/Bar.cpp         compile ONE file (does my edit compile?)
  scripts/ibuild.py run -- CMD...               any other build command, under the lock
  scripts/ibuild.py linux [build|test|shell]    OrbStack container, started on demand, stopped after

Modes: dev (build-dev, Debug, arm64) | asan (build-asan, +address,undefined) |
       release (build, universal arm64+x86_64 - shipping only)
Exit: 0 ok, 1 build failed, 2 usage/lock timeout.
"""
import argparse, os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = "/tmp/infinite-build.lock"          # machine-wide, shared by every worktree/session
MODES = {  # dir, cmake args, per-compile RAM estimate (MB), -j cap
    "dev":     ("build-dev",  ["-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_OSX_ARCHITECTURES=arm64"], 450, 4),
    "asan":    ("build-asan", ["-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_OSX_ARCHITECTURES=arm64",
                               "-DINFINITE_SANITIZE=address,undefined"], 800, 3),
    "release": ("build",      ["-DCMAKE_BUILD_TYPE=Release"], 600, 3),
}


def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, **kw).stdout


def machine():
    page = 16384
    vm = {m.group(1): int(m.group(2)) for m in re.finditer(r"Pages ([a-z ]+):\s+(\d+)", sh(["vm_stat"]))}
    avail = (vm.get("free", 0) + vm.get("inactive", 0) + vm.get("speculative", 0) + vm.get("purgeable", 0)) * page / 2**20
    sw = re.search(r"used = ([\d.]+)M", sh(["sysctl", "-n", "vm.swapusage"]))
    swap = float(sw.group(1)) if sw else 0.0
    load = os.getloadavg()[0]
    return avail, swap, load, os.cpu_count() or 4


def pick_jobs(mode):
    avail, swap, load, ncpu = machine()
    _, _, per_job, cap = MODES[mode]
    j = min(cap, ncpu - 2, int(avail // per_job))
    if swap > 4096: j = min(j, 2)           # already swapping: go gentle
    if swap > 8192: j = 1
    if load > ncpu * 1.5: j = min(j, 2)     # something else is hammering the CPU
    return max(1, j)


def other_builds():
    out = sh(["ps", "-Ao", "pid,etime,command"])
    return [l.strip()[:140] for l in out.splitlines() if re.search(r"\bmake\b.*-j|cmake --build|xcodebuild|ninja", l) and "ibuild" not in l and "grep" not in l]


def lock_holder():
    try:
        pid, info = open(LOCK + "/owner").read().split(" ", 1)
        os.kill(int(pid), 0)
        return int(pid), info.strip()
    except (OSError, ValueError):
        return None


def acquire(wait, what):
    start = time.time()
    while True:
        try:
            os.mkdir(LOCK)
            open(LOCK + "/owner", "w").write(f"{os.getpid()} {what} (since {time.strftime('%H:%M:%S')})")
            return True
        except FileExistsError:
            if lock_holder() is None:       # stale: owner died
                shutil.rmtree(LOCK, ignore_errors=True)
                continue
            if time.time() - start > wait:
                print(f"ibuild: lock still held after {wait}s by: {lock_holder()[1]}", file=sys.stderr)
                return False
            if int(time.time() - start) % 30 == 0:
                print(f"ibuild: waiting for build lock held by: {lock_holder()[1]}", flush=True)
            time.sleep(5)


def release():
    shutil.rmtree(LOCK, ignore_errors=True)


def configure(mode):
    d, args, _, _ = MODES[mode]
    path = os.path.join(ROOT, d)
    if os.path.exists(path + "/CMakeCache.txt"):
        return path
    extra = ["-DCMAKE_CXX_COMPILER_LAUNCHER=ccache", "-DCMAKE_C_COMPILER_LAUNCHER=ccache"] if shutil.which("ccache") else []
    print(f"ibuild: configuring {d} (first build in this dir compiles everything once)", flush=True)
    r = subprocess.run(["cmake", "-S", ROOT, "-B", path, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", *args, *extra])
    if r.returncode:
        sys.exit(1)
    return path


def stale_count(path, target):
    if not os.path.exists(path + "/Makefile"):
        return None
    out = sh(["make", "-C", path, "-n", "-s", target])
    return len(re.findall(r"Building (?:C|CXX|OBJC|OBJCXX) object", out)), len(re.findall(r"Linking", out))


def cmd_status(a):
    avail, swap, load, ncpu = machine()
    print(f"cores {ncpu} | avail RAM ~{avail:.0f} MB | swap used {swap:.0f} MB | load {load:.1f}")
    for m in MODES:
        print(f"  recommended -j for {m:8s}: {pick_jobs(m)}")
    h = lock_holder()
    print("build lock:", h[1] if h else "free")
    ob = other_builds()
    print("other build processes:", *(ob or ["none"]), sep="\n  ")
    print("ccache:", "yes" if shutil.which("ccache") else "NO (brew install ccache: worktrees recompile everything without it)")
    if swap > 4096 or load > ncpu * 1.5:
        print("VERDICT: machine is stressed. Do not start a build; wait or stop what is running.")


def cmd_plan(a):
    d, *_ = MODES[a.mode]
    path = os.path.join(ROOT, d)
    r = stale_count(path, a.target)
    if r is None:
        print(f"{d}: no build dir yet -> first build compiles everything (once). Not needed unless you must run the app.")
    elif r == (0, 0):
        print(f"{d}: up to date. SKIP the build.")
    else:
        print(f"{d}: {r[0]} files to compile, {r[1]} link(s). -j{pick_jobs(a.mode)} recommended.")


def cmd_build(a):
    d, *_ = MODES[a.mode]
    if not a.force:
        r = stale_count(os.path.join(ROOT, d), a.target)
        if r == (0, 0):
            print(f"ibuild: {d} up to date, nothing to do.")
            return 0
    if not acquire(a.wait, f"{a.mode} {a.target} in {os.path.basename(ROOT)}"):
        return 2
    try:
        path = configure(a.mode)
        jobs = a.jobs or pick_jobs(a.mode)
        print(f"ibuild: {a.mode} -> {os.path.basename(path)}  -j{jobs}  (nice 10)", flush=True)
        log = f"/tmp/ibuild_{a.mode}.log"
        with open(log, "w") as lf:
            p = subprocess.Popen(["nice", "-n", "10", "cmake", "--build", path, "--target", a.target, "-j", str(jobs)],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in p.stdout:
                lf.write(line)
                if re.search(r"error|warning: unused|Linking|\*\*\*|Built target " + re.escape(a.target), line):
                    print(line, end="")
            rc = p.wait()
        print(f"ibuild: {'OK' if rc == 0 else 'FAILED'} (full log {log})")
        return 0 if rc == 0 else 1
    finally:
        release()


def cmd_obj(a):
    """Compile one translation unit using the dev dir's own flags. -fsyntax-only is far cheaper
    than building the object, but only if compile_commands.json exists."""
    path = os.path.join(ROOT, MODES["dev"][0])
    cc = path + "/compile_commands.json"
    if not os.path.exists(cc):
        if not acquire(a.wait, "configure dev"):
            return 2
        try:
            configure("dev")
        finally:
            release()
        if not os.path.exists(cc):
            print("ibuild: configure did not produce compile_commands.json", file=sys.stderr)
            return 2
    import json
    src = os.path.abspath(a.file)
    for e in json.load(open(cc)):
        if os.path.abspath(e["file"]) == src:
            cmd = re.sub(r" -c(?= )", "", re.sub(r" -o \S+", "", e["command"])) + " -fsyntax-only"
            if not acquire(a.wait, f"syntax {os.path.basename(src)}"):
                return 2
            try:
                return subprocess.call(["nice", "-n", "10", "sh", "-c", cmd], cwd=e["directory"])
            finally:
                release()
    print(f"ibuild: {a.file} not in compile_commands.json", file=sys.stderr)
    return 2


def cmd_run(a):
    """Run any build-ish command under the machine lock at low priority (used by driver.sh)."""
    if not a.argv:
        return 2
    if not acquire(a.wait, "run " + os.path.basename(a.argv[0])):
        return 2
    try:
        return subprocess.call(["nice", "-n", "10"] + a.argv)
    finally:
        release()


def cmd_linux(a):
    """OrbStack is a VM: ~2+ GB RAM while up. Start only for the Linux job, stop right after."""
    was_up = subprocess.run(["docker", "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not acquire(a.wait, f"linux {a.sub}"):
        return 2
    try:
        if not was_up:
            print("ibuild: starting OrbStack", flush=True)
            subprocess.run(["orb", "start"])
        rc = subprocess.call(["nice", "-n", "10", os.path.join(ROOT, "tools/linux/local.sh"), a.sub])
    finally:
        if not was_up:
            print("ibuild: stopping OrbStack to give the RAM back", flush=True)
            subprocess.run(["orb", "stop"])
        release()
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("status", cmd_status), ("plan", cmd_plan), ("build", cmd_build)):
        p = sp.add_parser(name); p.set_defaults(fn=fn)
        if name != "status":
            p.add_argument("--mode", choices=MODES, default="dev")
            p.add_argument("--target", default="Infinite")
        if name == "build":
            p.add_argument("--jobs", type=int); p.add_argument("--wait", type=int, default=1800)
            p.add_argument("--force", action="store_true", help="skip the up-to-date check")
    p = sp.add_parser("run"); p.set_defaults(fn=cmd_run); p.add_argument("--wait", type=int, default=1800); p.add_argument("argv", nargs=argparse.REMAINDER)
    p = sp.add_parser("jobs"); p.set_defaults(fn=lambda a: print(pick_jobs("dev")))
    p = sp.add_parser("obj"); p.set_defaults(fn=cmd_obj); p.add_argument("file"); p.add_argument("--wait", type=int, default=600)
    p = sp.add_parser("linux"); p.set_defaults(fn=cmd_linux); p.add_argument("sub", nargs="?", default="build"); p.add_argument("--wait", type=int, default=1800)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
