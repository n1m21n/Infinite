#!/usr/bin/env python3
"""One command after any UI/UX change: every mechanical UI check, one pass/fail table.

    python3 tools/design/ui_check.py                       # lints + coverage + goldens (build-dev binary)
    python3 tools/design/ui_check.py --binary PATH         # goldens against another binary
    python3 tools/design/ui_check.py --base OLD_BINARY     # also: every test patch saves byte-identical vs OLD
    python3 tools/design/ui_check.py --fast                # lints + coverage only (no app launch)
    python3 tools/design/ui_check.py --update              # after a reviewed improvement: store the new coverage floor

Checks:
  1 ratchet        tools/design/ratchet.py: colours, raw widgets, sizes, fonts, roundings, popups, layout calls,
                   token names, generated tokens current, ui_audit counts - nothing may go up (also the pre-commit
                   hook and the CI "Design ratchet" job)
  2 hardcoded      tools/design/ui_audit.py: 0 findings outside the design kit
  3 coverage       tools/design/param_coverage.py: share of node controls drawn from param tables never drops
  4 goldens        tools/design/golden.py, run twice: every page matches both times (a single run can catch a fade)
  5 patches        --base only: Infinite --canonicalize --keep-ids of every tests/**/*.inf, old vs new binary

Not covered (by eye, .claude/skills/node-ui-pillars): knob grid, mod-dot centring, mix slot, light + dark contrast;
and frame time (scripts/bench/ab.sh) when drawing code changed.
"""
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / "tools/design/ui_check_baseline.json"
PY = sys.executable


def run(cmd, timeout=1800):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def check_ratchet():
    rc, out = run([PY, "tools/design/ratchet.py"])
    ups = [l for l in out.splitlines() if l.startswith("RATCHET")]
    lints = next((l for l in out.splitlines() if l.startswith("lints:")), "")
    raw = re.search(r"raw widgets (\d+)", out)
    detail = (f"{len(ups)} went up: " + "; ".join(ups[:3])) if ups else \
        f"nothing went up (raw widgets {raw.group(1) if raw else '?'}; {lints[7:120]}...)"
    return rc == 0, detail


def check_hardcoded():
    rc, out = run([PY, "tools/design/ui_audit.py"])
    m = re.search(r"^TOTAL\s+(\d+)\s+(\d+)", out, re.M)
    if not m:
        return False, "ui_audit printed no TOTAL line"
    total, outside = int(m.group(1)), int(m.group(2))
    raw = re.search(r"raw ImGui calls outside the kit: (\d+)", out)
    return outside == 0, f"{outside} outside the kit ({total} in all; raw ImGui calls outside kit {raw.group(1) if raw else '?'})"


def check_coverage(update):
    rc, out = run([PY, "tools/design/param_coverage.py"])
    m = re.search(r"descriptor rows (\d+), custom rows (\d+), hand calls (\d+): ([\d.]+)%", out)
    if not m:
        return False, "param_coverage printed no summary"
    pct, hand = float(m.group(4)), int(m.group(3))
    base = json.loads(BASELINE.read_text()) if BASELINE.exists() else {}
    floor = base.get("coverage_pct", 0.0)
    if update and pct >= floor:
        base["coverage_pct"] = pct
        BASELINE.write_text(json.dumps(base, indent=1) + "\n")
        floor = pct
    return pct >= floor, f"{pct}% from tables, {hand} hand calls (floor {floor}%)"


def check_goldens(binary):
    if not pathlib.Path(binary).exists():
        return False, f"no binary at {binary} (build it or pass --binary)"
    fails = []
    for i in (1, 2):
        rc, out = run([PY, "tools/design/golden.py", "--binary", binary])
        pages = re.findall(r"^(\S+): mean diff .*?(ok|DIFFERS)$", out, re.M)
        if not pages:
            return False, f"run {i}: golden.py printed no pages ({out.strip().splitlines()[-1:]})"
        fails += [f"run {i} {p}" for p, v in pages if v != "ok"]
    return not fails, (f"{len(pages)} pages ok on both runs" if not fails else "; ".join(fails))


def verdict(msg):
    """A patch that does not save (the deliberately broken test patches) is compared by its verdict - ok, errors,
    warnings - not by the whole report, which also carries elapsed_ms and the temp out path."""
    for line in reversed(msg.strip().splitlines()):
        try:
            r = json.loads(line)
        except ValueError:
            continue
        return json.dumps([r.get("ok"), r.get("errors"), r.get("warnings")], sort_keys=True)
    return msg.strip().splitlines()[-1:]


def check_patches(base, binary):
    patches = sorted(p for p in (ROOT / "tests").rglob("*.inf"))
    same = differ = 0
    bad = []
    with tempfile.TemporaryDirectory() as td:
        for i, p in enumerate(patches):
            outs = []
            for tag, exe in (("a", base), ("b", binary)):
                o = pathlib.Path(td) / f"{i}{tag}.inf"
                rc, msg = run([exe, "--canonicalize", str(p), str(o), "--keep-ids"], timeout=120)
                outs.append((rc, o.read_bytes() if o.exists() else verdict(msg)))
            if outs[0] == outs[1]:
                same += 1
            else:
                differ += 1
                bad.append(str(p.relative_to(ROOT)))
    return differ == 0, f"{same}/{len(patches)} identical" + (f"; differ: {', '.join(bad[:4])}" if bad else "")


def main():
    fast, update = "--fast" in sys.argv, "--update" in sys.argv
    binary = arg("--binary", str(ROOT / "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    base = arg("--base")
    rows = [("1 ratchet", *check_ratchet()), ("2 hardcoded", *check_hardcoded()), ("3 coverage", *check_coverage(update))]
    if not fast:
        rows.append(("4 goldens", *check_goldens(binary)))
        if base:
            rows.append(("5 patches", *check_patches(base, binary)))
    w = max(len(r[0]) for r in rows)
    print()
    for name, ok, detail in rows:
        print(f"{name.ljust(w)}  {'PASS' if ok else 'FAIL'}  {detail}")
    if fast:
        print("(goldens skipped: --fast)")
    elif not base:
        print("(patches skipped: pass --base OLD_BINARY to compare saves)")
    print("by eye: node-ui-pillars checklist in light and dark; scripts/bench/ab.sh if drawing code changed")
    return 0 if all(ok for _, ok, _ in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
