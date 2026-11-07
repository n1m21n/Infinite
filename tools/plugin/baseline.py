#!/usr/bin/env python3
"""Record the plugin plan's baseline B-1..B-8 (docs/plans/vst-au/README.md, section 4).

Runs once before chunk 1 and writes tests/plugin/baseline.json. Every row says how it
was measured; rows the tools cannot measure yet are written as "pending" with the reason,
never guessed.

    python3 tools/plugin/baseline.py --app build/Infinite.app --spike build-spike
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile

PATCH = "assets/templates/first-music-image.inf"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def timed(cmd):
    """Runs cmd under /usr/bin/time -l; returns (real s, user s, sys s, max RSS MB)."""
    r = run(["/usr/bin/time", "-l"] + cmd)
    if r.returncode != 0:
        raise RuntimeError(f"{cmd} failed:\n" + r.stdout[-800:])
    err = r.stderr
    m = re.search(r"([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys", err)
    rss = re.search(r"(\d+)\s+maximum resident set size", err)
    if not m or not rss:
        raise RuntimeError("time output not understood:\n" + err[-800:])
    return float(m[1]), float(m[2]), float(m[3]), int(rss[1]) / 1e6


def b1(app):
    out = run(["du", "-sk", app]).stdout.split()[0]
    return {"value_mb": round(int(out) / 1024, 1), "how": f"du -sk {app}"}


def b2_b3_b4(binary, patch):
    # B-3: a 0.1 s render is dominated by start-up, so its wall clock is the cold start.
    out = os.path.join(tempfile.mkdtemp(), "summary.json")
    real3, _, _, _ = timed([binary, "--audio-summary", patch, out, "--duration", "0.1"])
    real, user, sys_, rss = timed([binary, "--audio-summary", patch, out, "--duration", "10"])
    return (
        {"value_mb": round(rss, 1), "how": f"max RSS of headless --audio-summary {patch} 10 s",
         "note": "headless peak, not idle; true idle RSS needs plugin mode (step 1.4)"},
        {"value_s": round(real3, 3), "how": "wall clock of headless --audio-summary --duration 0.1"},
        {"value_cpu_s_per_s": round((user + sys_) / 10, 3), "wall_s": real,
         "how": f"(user+sys)/10 for 10 s offline render of {patch}",
         "note": "offline render of the stand-in patch; R1 is frozen in step 1.4 (needs host input)"},
    )


def b5(spike):
    exe = os.path.join(spike, "bin/Release/spike_bench")
    eng = os.path.join(spike, "bin/Release/infinite-spike-engine")
    rows = [json.loads(l) for l in run([exe, eng, "4000"]).stdout.splitlines() if l.startswith("{")]
    return {"rows": rows, "how": "tools/plugin/spike/spike_bench, paced at 48 kHz, os_sync wake"}


def b7(binary, patch):
    r = run([sys.executable, "tools/determinism-check.py", patch, "--audio-only",
             "--audio-seconds", "4", "--binary", binary])
    return {"identical": r.returncode == 0, "patch": patch,
            "known_exception": "Field Notes + modulation, ~0.1 dB (docs/headless/GUIDE.md)",
            "how": "tools/determinism-check.py --audio-only"}


def b8(spike):
    vst = os.path.join(spike, "VST3/Release/InfiniteSpike.vst3")
    v = run([os.path.join(spike, "bin/Release/validator"), vst])
    m = re.search(r"Result:\s*(\d+)\s*tests passed,\s*(\d+)\s*tests failed", v.stdout)
    au = run(["auval", "-v", "aufx", "Ispk", "Infn"])
    return {"vst3_validator": {"passed": int(m[1]) if m else None, "failed": int(m[2]) if m else None},
            "auval": "PASS" if "AU VALIDATION SUCCEEDED" in au.stdout else "not run (AU not installed)",
            "how": "SDK validator on the spike VST3; auval -v when the spike AU is installed",
            "note": "spike shell stands in for the empty 'again' sample; pluginval waits for step 1.7"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="build/Infinite.app")
    ap.add_argument("--spike", default="build-spike")
    ap.add_argument("--patch", default=PATCH)
    ap.add_argument("--out", default="tests/plugin/baseline.json")
    a = ap.parse_args()
    binary = os.path.join(a.app, "Contents/MacOS/Infinite")
    b2, b3, b4 = b2_b3_b4(binary, a.patch)
    data = {
        "recorded": datetime.date.today().isoformat(),
        "machine": run(["sysctl", "-n", "machdep.cpu.brand_string"]).stdout.strip(),
        "app_version": run([binary, "--version"]).stdout.strip(),
        "B-1": b1(a.app), "B-2": b2, "B-3": b3, "B-4": b4, "B-5": b5(a.spike),
        "B-6": {"status": "pending", "why": "readback_bench is built with the GUI pixel path (step 2.3)"},
        "B-7": b7(binary, a.patch), "B-8": b8(a.spike),
    }
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
