#!/usr/bin/env python3
"""Which code did a test or fuzz run never reach?

Needs a coverage build:
  cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=RelWithDebInfo -DINFINITE_COVERAGE=ON
  cmake --build build-cov --target Infinite

  coverage.py run  PROFDIR -- <command ...>   run one command with profiling into PROFDIR
                                              (each process writes its own .profraw, so
                                              param_fuzz.py / stress_kit.py runs work as-is:
                                              set LLVM_PROFILE_FILE=PROFDIR/%p.profraw)
  coverage.py report BIN PROFDIR [--out FILE.md] [--under src/nodes] [--top 40]

The report merges every .profraw, then lists, per source area (src/nodes,
src/app/bodies, src/core, src/audio, ...), line coverage and the largest files
and functions with zero hits. That list is the next spree's target: code no
fuzz case, self-test or smoke row reaches.
Test: `coverage.py selftest`.
"""
import collections
import glob
import json
import os
import subprocess
import sys


def tool(name):
    try:
        return subprocess.check_output(["xcrun", "--find", name], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return name


def cmd_run(a):
    prof = os.path.abspath(a[0])
    os.makedirs(prof, exist_ok=True)
    cmd = a[a.index("--") + 1:]
    env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(prof, "%p.profraw"))
    sys.exit(subprocess.call(cmd, env=env))


def area(path):
    p = path.split("/src/", 1)[-1] if "/src/" in path else path
    parts = p.split("/")
    return "src/" + "/".join(parts[:2]) if len(parts) > 2 else "src/" + parts[0]


def summarize(export, under=None, top=40):
    """export: llvm-cov export -summary-only JSON. Returns markdown."""
    files = export["data"][0]["files"]
    by_area = collections.defaultdict(lambda: [0, 0])
    zero = []
    for f in files:
        name = f["filename"]
        if "/src/" not in name or "/_deps/" in name or "/external/" in name:
            continue
        if under and under not in name:
            continue
        lines = f["summary"]["lines"]
        a = by_area[area(name)]
        a[0] += lines["covered"]
        a[1] += lines["count"]
        if lines["count"] >= 20 and lines["covered"] == 0:
            zero.append((lines["count"], name.split("/src/", 1)[-1]))
    out = ["| Area | Lines hit | Lines | % |", "|---|---|---|---|"]
    tot = [0, 0]
    for k in sorted(by_area, key=lambda k: by_area[k][1] - by_area[k][0], reverse=True):
        c, n = by_area[k]
        tot[0] += c
        tot[1] += n
        out.append(f"| {k} | {c} | {n} | {100.0 * c / max(n, 1):.1f} |")
    out.append(f"| **all** | {tot[0]} | {tot[1]} | {100.0 * tot[0] / max(tot[1], 1):.1f} |")
    out += ["", f"## Files never reached ({len(zero)}; biggest first)", ""]
    out += [f"- `src/{n}` ({c} lines)" for c, n in sorted(zero, reverse=True)[:top]]
    return "\n".join(out) + "\n"


def cmd_report(a):
    binp, prof = a[0], os.path.abspath(a[1])
    out = a[a.index("--out") + 1] if "--out" in a else None
    under = a[a.index("--under") + 1] if "--under" in a else None
    top = int(a[a.index("--top") + 1]) if "--top" in a else 40
    raws = glob.glob(os.path.join(prof, "*.profraw"))
    if not raws:
        sys.exit(f"no .profraw files in {prof}")
    data = os.path.join(prof, "merged.profdata")
    subprocess.check_call([tool("llvm-profdata"), "merge", "-sparse", "-o", data] + raws)
    j = json.loads(subprocess.check_output([tool("llvm-cov"), "export", "-summary-only",
                                            "-instr-profile=" + data, binp], text=True))
    md = f"# Coverage: {len(raws)} processes\n\n" + summarize(j, under, top)
    print(md)
    if out:
        open(out, "w").write(md)


def selftest():
    j = {"data": [{"files": [
        {"filename": "/r/src/nodes/A.cpp", "summary": {"lines": {"covered": 0, "count": 50}}},
        {"filename": "/r/src/nodes/B.cpp", "summary": {"lines": {"covered": 10, "count": 10}}},
        {"filename": "/r/src/app/bodies/C.cpp", "summary": {"lines": {"covered": 5, "count": 40}}},
        {"filename": "/r/build/_deps/glfw/src/x.c", "summary": {"lines": {"covered": 0, "count": 999}}},
    ]}]}
    md = summarize(j)
    assert "| src/nodes | 10 | 60 | 16.7 |" in md, md
    assert "| src/app/bodies | 5 | 40 | 12.5 |" in md, md
    assert "`src/nodes/A.cpp` (50 lines)" in md and "glfw" not in md, md
    assert area("/r/src/core/field/X.cpp") == "src/core/field"
    print("coverage selftest ok")


def main():
    a = sys.argv[1:]
    if a[:1] == ["selftest"]:
        return selftest()
    if a[:1] == ["run"]:
        return cmd_run(a[1:])
    if a[:1] == ["report"]:
        return cmd_report(a[1:])
    sys.exit(__doc__)


if __name__ == "__main__":
    main()
