#!/usr/bin/env python3
"""Determinism check: every type's default patch from a coverage sweep, rendered twice in --batch processes
with the job order reversed between the two passes, so each patch follows different neighbours and sits at a
different place in its process. A type whose two renders differ leaks state between jobs (a spawn counter, a
clock, a cache) or reads something outside the patch (wall-clock, user prefs).

Seeds the sweep pinned are stripped, so seedless patches are tested as a user would save them.
Both passes run with --isolated: the user's profile is not an input.

  determinism.py --sweep <coverage out dir> --out <dir> [--bin path] [--chunk 40] [--jobs 3]
Writes <out>/determinism.json and prints the types that differ.
"""
import argparse, json, os, re, shutil, subprocess, sys
import concurrent.futures as cf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sweep import Sweep, ROOT, TIMES, TIMES_3D, SIZE, AUDIO_SECONDS, sidecar  # noqa: E402

TOL = 1e-3  # mean absolute difference per pixel/sample; GPU float noise is far below this


def collect(sweep_dir, out):
    """(type dir name, channel, is_3d, stripped patch path) for every type whose defA rendered."""
    jobs = []
    types = os.path.join(sweep_dir, "types")
    for name in sorted(os.listdir(types)):
        src = os.path.join(types, name, "defA.inf")
        rep = os.path.join(types, name, "defA.json")
        if not os.path.exists(src) or not os.path.exists(rep):
            continue
        try:
            r = json.load(open(rep))
        except ValueError:
            continue
        if r.get("ok") is False:
            continue
        chan = "image" if "frame_stats" in r else "audio"
        text = open(src).read()
        is_3d = "Image Source" in text and "f width 128" in text
        text = re.sub(r"(?m)^  f seed [^\n]*\n", "", text)
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, "patch.inf")
        open(p, "w").write(text)
        jobs.append((name, chan, is_3d, p))
    return jobs


def run_pass(binary, jobs, out, tag, chunk, workers):
    """Render jobs in order, `chunk` per process, grouped by channel and time list as the sweep does."""
    groups = {}
    for name, chan, is_3d, p in jobs:
        key = (chan, TIMES_3D if is_3d else TIMES)
        d = os.path.join(out, name)
        o = os.path.join(d, tag + "/") if chan == "image" else os.path.join(d, tag + ".json")
        w = os.path.join(d, tag + ".wav") if chan == "audio" else ""
        groups.setdefault(key, []).append((p, o, w))
    chunks = []
    for (chan, times), lines in groups.items():
        for i in range(0, len(lines), chunk):
            chunks.append((chan, times, lines[i:i + chunk], f"{tag}-{chan}-{times}-{i}"))

    def one(c):
        """Run a chunk; a process that stops early is restarted after the job it stopped on."""
        chan, times, lines, name = c
        stopped = []
        while lines:
            first, rest = lines[0], lines[1:]
            lst = os.path.join(out, name + ".batch.txt")
            with open(lst, "w") as f:
                for p, o, w in rest:
                    f.write(f"{p}\t{o}" + (f"\t{w}" if w else "") + "\n")
            budget = str(30 + 3 * len(lines))
            if chan == "image":
                cmd = [binary, "--isolated", "--frame", first[0], times, first[1], "--size", SIZE, "--lenient",
                       "--timeout", budget, "--batch", lst]
            else:
                cmd = [binary, "--isolated", "--audio-summary", first[0], first[1], "--duration", AUDIO_SECONDS,
                       "--wav", first[2], "--lenient", "--timeout", budget, "--batch", lst]
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=60 + 4 * len(lines))
                tail = (r.stdout.strip().splitlines() or [""])[-1][:200]
            except subprocess.TimeoutExpired:
                tail = "TIMEOUT"
            left = [x for x in lines if not os.path.exists(sidecar(x[1]))]
            if left:
                stopped.append((left[0][0], tail))
                left = left[1:]
            lines = left
        return stopped

    with cf.ThreadPoolExecutor(workers) as ex:
        return [s for r in ex.map(one, chunks) for s in r]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sweep", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--chunk", type=int, default=40)
    ap.add_argument("--jobs", type=int, default=3)
    a = ap.parse_args()
    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    os.makedirs(a.out)
    jobs = collect(a.sweep, a.out)
    print(f"{len(jobs)} types", flush=True)
    stopped = run_pass(a.bin, jobs, a.out, "fwd", a.chunk, a.jobs)
    stopped += run_pass(a.bin, list(reversed(jobs)), a.out, "rev", a.chunk, a.jobs)
    for p, tail in stopped:
        print(f"  stopped on {os.path.basename(os.path.dirname(p))}: {tail}")
    rows, differ, missing = [], [], []
    for name, chan, _, _ in jobs:
        d = os.path.join(a.out, name)
        load = Sweep.load_image if chan == "image" else Sweep.load_audio
        delta = Sweep.diff(load(d, "fwd"), load(d, "rev"))
        rows.append({"type": name, "channel": chan, "diff": delta})
        if delta is None:
            missing.append(name)
        elif delta > TOL:
            differ.append((name, delta))
    json.dump({"tol": TOL, "rows": rows, "stopped": stopped}, open(os.path.join(a.out, "determinism.json"), "w"), indent=1)
    print(f"same: {len(rows) - len(differ) - len(missing)}  differ: {len(differ)}  not rendered: {len(missing)}")
    for name, delta in sorted(differ, key=lambda x: -x[1]):
        print(f"  differ  {name}  {delta:.4g}")
    for name in missing:
        print(f"  missing {name}")


if __name__ == "__main__":
    main()
