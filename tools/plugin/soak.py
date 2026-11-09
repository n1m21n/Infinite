#!/usr/bin/env python3
"""Soak for the Infinite plugin (docs/plans/vst-au/README.md step 2.7: M-7, M-8, M-9).

    tools/plugin/soak.py [--minutes 60] [--instruments 2] [--fx 2] [--build build-plugin]
                         [--engine PATH] [--out tests/plugin/soak.json]

Runs `plugin_shell_check --soak`: one host audio thread at 48 kHz / 256 drives the
Instruments on R1 (a note every 0.5 s) and the FX on R2 (sine + noise), after a 20 s idle
phase. No DAW needed. While it runs this script samples every engine's resident memory and
the system's swap.

    M-7  0 blocks played as silence because an engine was late (the shell's xrun counter)
    M-8  host-thread CPU inside process() <= 3 % of one core per instance
    M-9  idle engine memory <= plugin-mode B-2 + 10 % (310 MB -> 341 MB), read as physical
         footprint (resident + compressed + swapped: what Activity Monitor shows; plain RSS
         reads ~30 MB once the compressor has the pages, so it says nothing on a busy Mac),
         and the engines push nothing more to swap while playing: their summed footprint
         grows <= 64 MB. System swap is printed
         as well, but other apps move it by hundreds of MB on a busy 8 GB Mac.

Also reported: memory growth per engine over the run (a leak shows here), the host thread's
longest process() call and how often one cycle overran its block. Exit 0 when all pass.
Do not run a build or another benchmark at the same time.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

B2_PLUGIN_MB = 310.0  # README 6, B-2: engine peak RSS in plugin mode (hidden window, GL up)
SWAP_TOLERANCE_MB = 64.0


def rss_mb(pid):
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return int(out) / 1024.0 if out.isdigit() else None


def footprint_mb(pid):
    """Physical footprint (what Activity Monitor shows: resident + compressed + swapped dirty
    memory). Plain RSS hides what the memory compressor took, so it reads low on a busy 8 GB Mac."""
    if sys.platform == "darwin":
        import ctypes
        lib = ctypes.CDLL("/usr/lib/libproc.dylib")
        buf = (ctypes.c_uint64 * 40)()  # rusage_info_v2; ri_phys_footprint is the 10th uint64 after the 16-byte uuid
        if lib.proc_pid_rusage(int(pid), 2, ctypes.byref(buf)) == 0:
            return buf[2 + 7] / (1024.0 * 1024.0)
        return None
    return rss_mb(pid)


def swap_used_mb():
    out = subprocess.run(["sysctl", "-n", "vm.swapusage"], capture_output=True, text=True).stdout
    m = re.search(r"used = ([\d.]+)M", out)
    return float(m.group(1)) if m else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--minutes", type=float, default=60.0)
    ap.add_argument("--instruments", type=int, default=2)
    ap.add_argument("--fx", type=int, default=2)
    ap.add_argument("--idle", type=int, default=20, help="idle seconds before playing")
    ap.add_argument("--report", type=int, default=10, help="seconds between samples")
    ap.add_argument("--build", default="build-plugin")
    ap.add_argument("--engine", default="", help="Infinite binary (default $INFINITE_PLUGIN_ENGINE or build/Infinite.app)")
    ap.add_argument("--out", default="", help="write the result JSON here")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    check = os.path.join(root, a.build, "bin", "Release", "plugin_shell_check")
    inst = os.path.join(root, a.build, "VST3", "Release", "Infinite Instrument.vst3")
    fx = os.path.join(root, a.build, "VST3", "Release", "Infinite FX.vst3")
    r1 = os.path.join(root, "tests", "plugin", "patches", "R1-instrument.inf")
    r2 = os.path.join(root, "tests", "plugin", "patches", "R2-fx.inf")
    engine = a.engine or os.environ.get("INFINITE_PLUGIN_ENGINE") or os.path.join(root, "build", "Infinite.app", "Contents", "MacOS", "Infinite")
    for path in (check, inst, fx, r1, r2, engine):
        if not os.path.exists(path):
            print(f"missing: {path}")
            return 2

    env = dict(os.environ, INFINITE_PLUGIN_ENGINE=engine)
    cmd = [check, "--soak", str(a.minutes), inst, fx, r1, r2, str(a.instruments), str(a.fx), str(a.idle), str(a.report)]
    swap0 = swap_used_mb()
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, text=True, bufsize=1)
    idle_rss, first_play, last_rss, peak, idle_fp, last_fp = {}, {}, {}, {}, {}, {}
    swap_idle, lines, checks = None, [], []
    for line in proc.stdout:
        line = line.rstrip("\n")
        if line.startswith("{\"soak\""):
            row = json.loads(line)
            pids = row["pids"]
            mem = {p: rss_mb(p) for p in pids if p > 0}
            fp = {p: footprint_mb(p) for p in pids if p > 0}
            row["rss_mb"] = [round(mem.get(p) or 0.0, 1) for p in pids]
            row["footprint_mb"] = [round(fp.get(p) or 0.0, 1) for p in pids]
            row["swap_used_mb"] = swap_used_mb()
            lines.append(row)
            if row["phase"] == "idle":
                idle_rss = dict(mem)
                idle_fp = dict(fp)
                swap_idle = row["swap_used_mb"]
            else:
                for p, v in fp.items():
                    if v is None:
                        continue
                    first_play.setdefault(p, v)
                    last_rss[p] = v
                    peak[p] = max(peak.get(p, 0.0), v)
                last_fp = dict(fp)
            print(f"  {row['phase']:4} {row['t']:7.1f} s  xruns {row['xruns']}  cpu % {row['cpu_pct']}  rss MB {row['rss_mb']}  footprint MB {row['footprint_mb']}  "
                  f"swap {row['swap_used_mb']:.0f} MB  overruns {row['host_overruns']}  max call {row['max_call_us']:.0f} us (inst {row['max_call_inst']}, cpu {row['max_call_cpu_us']:.0f} us, rt {row['rt']})", flush=True)
        else:
            print(line, flush=True)
            if line.startswith(("PASS", "FAIL")):
                checks.append(line)
    proc.wait()

    limit = B2_PLUGIN_MB * 1.1
    worst_idle = max(idle_fp.values(), default=0.0)
    growth = max((last_rss[p] - first_play[p] for p in last_rss if p in first_play), default=0.0)
    swap_end = lines[-1]["swap_used_mb"] if lines else swap0
    swap_growth = swap_end - (swap_idle if swap_idle is not None else swap0)
    fp_growth = sum((last_fp.get(p) or 0.0) - (idle_fp.get(p) or 0.0) for p in idle_fp if last_fp.get(p))
    m9_mem = 0 < worst_idle <= limit
    m9_swap = bool(last_fp) and fp_growth <= SWAP_TOLERANCE_MB
    print(f"{'PASS' if m9_mem else 'FAIL'}  M-9: idle engine footprint <= {limit:.0f} MB  worst {worst_idle:.0f} MB "
          f"({', '.join(f'{v:.0f}' for v in idle_fp.values())}; RSS {', '.join(f'{v:.0f}' for v in idle_rss.values())})")
    print(f"{'PASS' if m9_swap else 'FAIL'}  M-9: engines push nothing to swap while playing  footprint {fp_growth:+.1f} MB "
          f"summed over engines (tolerance {SWAP_TOLERANCE_MB:.0f}); idle footprint {', '.join(f'{v:.0f}' for v in idle_fp.values())} MB")
    print(f"      system swap {swap_growth:+.0f} MB (idle {swap_idle if swap_idle is not None else swap0:.0f} MB, end {swap_end:.0f} MB; "
          f"before the engines started {swap0:.0f} MB)")
    print(f"      memory growth per engine while playing: worst {growth:+.1f} MB; peak {max(peak.values(), default=0):.0f} MB")

    ok = proc.returncode == 0 and m9_mem and m9_swap
    if a.out:
        out = os.path.join(root, a.out) if not os.path.isabs(a.out) else a.out
        with open(out, "w") as f:
            json.dump({
                "recorded": time.strftime("%Y-%m-%d %H:%M"),
                "minutes": a.minutes, "instruments": a.instruments, "fx": a.fx, "rate": 48000, "block": 256,
                "checks": checks, "m9_idle_rss_mb": [round(v, 1) for v in idle_rss.values()], "m9_limit_mb": round(limit, 1),
                "swap_growth_mb": round(swap_growth, 1), "footprint_growth_mb": round(fp_growth, 1),
                "m9_idle_footprint_mb": [round(v, 1) for v in idle_fp.values()], "rss_growth_mb": round(growth, 1), "pass": ok, "samples": lines,
            }, f, indent=1)
        print(f"wrote {out}")
    print("ALL PASS" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
