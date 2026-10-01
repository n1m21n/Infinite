#!/usr/bin/env python3
"""Does a patch render the same twice? (docs/fix-briefs/headless-engine.md, block 5)

Runs `Infinite --frame` and `Infinite --audio-summary --wav` on a patch in two separate
processes and compares the bytes. Exit 0 = identical, 1 = a difference (the first differing
frame is named), 2 = usage / the tool itself failed.

    python3 tools/determinism-check.py tests/headless/format/modulation.inf
    python3 tools/determinism-check.py patch.inf --times 0,0.5,1 --binary build/Infinite.app/Contents/MacOS/Infinite

A patch listed in tools/determinism-by-design.txt (one patch path per line, `#` comments) is
reported `nondeterministic_by_design` and passes: live input, plugins with their own clocks, and
anything else that cannot be bit-exact on purpose.

The frames are compared as decoded-file bytes, so this is exact on one machine. Goldens across
GPUs need a tolerance and belong to tools/render-check.py, not here.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

DEFAULT_BINARY = {
    "darwin": "build/Infinite.app/Contents/MacOS/Infinite",
    "win32": "build/Release/Infinite.exe",
}.get(sys.platform, "build/Infinite")


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(binary, args, timeout):
    p = subprocess.run([binary] + args, capture_output=True, text=True, timeout=timeout)
    last = (p.stdout.strip().splitlines() or ["{}"])[-1]
    try:
        status = json.loads(last)
    except ValueError:
        status = {}
    return p.returncode, status


def frames(binary, patch, times, out_dir, timeout):
    code, status = run(binary, ["--frame", patch, times, out_dir, "--lenient"], timeout)
    if code != 0:
        return code, status, []
    return 0, status, sorted(status.get("files", []))


def node_indices(patch):
    """The `node <index> <Category> <Type>` numbers in the patch file, in file order."""
    out = []
    with open(patch) as f:
        for line in f:
            m = re.match(r"node\s+(\d+)\s", line)
            if m:
                out.append(int(m.group(1)))
    return out


def first_differing_node(binary, patch, time, tmp, timeout):
    """Render each node's own image twice (separate processes) at `time`; the first node in
    file order whose bytes differ is where nondeterminism enters (or a node upstream of it that
    has no image of its own). None = every node image matched."""
    for idx in node_indices(patch):
        hashes = []
        for tag in ("a", "b"):
            d = os.path.join(tmp, f"n{idx}{tag}")
            code, status = run(binary, ["--frame", patch, str(time), d, "--node", str(idx), "--lenient"], timeout)
            files = sorted(status.get("files", [])) if code == 0 else []
            if not files:
                break # this node has no image output (audio / modulator): skip it
            hashes.append(sha(files[0]))
        if len(hashes) == 2 and hashes[0] != hashes[1]:
            return idx
    return None


def listed_by_design(patch):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "determinism-by-design.txt")
    if not os.path.exists(path):
        return False
    want = os.path.normpath(patch)
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line and os.path.normpath(line) == want:
                return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("patch")
    ap.add_argument("--times", default="0,0.5,1,2", help="--frame timestamps, seconds")
    ap.add_argument("--audio-seconds", type=float, default=2.0)
    ap.add_argument("--binary", default=DEFAULT_BINARY)
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--bisect", action="store_true",
                    help="when a frame differs, also name the first node (patch order) whose own image differs")
    ap.add_argument("--json", action="store_true", help="print the result as one JSON object")
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        print(f"error: no Infinite binary at {args.binary} (use --binary)", file=sys.stderr)
        return 2
    by_design = listed_by_design(args.patch)
    result = {"patch": args.patch, "deterministic": True, "nondeterministic_by_design": by_design,
              "frames_compared": 0, "first_differing_frame": None, "first_differing_node": None, "audio": "skipped"}

    with tempfile.TemporaryDirectory() as tmp:
        runs = []
        for tag in ("a", "b"):
            d = os.path.join(tmp, "f" + tag)
            code, status, files = frames(args.binary, args.patch, args.times, d, args.timeout)
            if code != 0:
                print(f"error: --frame failed (exit {code}): {json.dumps(status.get('errors', status))}", file=sys.stderr)
                return 2
            runs.append(files)
        for fa, fb in zip(*runs):
            result["frames_compared"] += 1
            if sha(fa) != sha(fb) and result["first_differing_frame"] is None:
                result["first_differing_frame"] = os.path.basename(fa)
                result["deterministic"] = False
                if args.bisect:
                    t = args.times.split(",")[result["frames_compared"] - 1]
                    result["first_differing_node"] = first_differing_node(args.binary, args.patch, t, tmp, args.timeout)

        wavs = []
        for tag in ("a", "b"):
            wav = os.path.join(tmp, tag + ".wav")
            code, status = run(args.binary, ["--audio-summary", args.patch, os.path.join(tmp, tag + ".json"),
                                             "--duration", str(args.audio_seconds), "--wav", wav, "--lenient"],
                               args.timeout)
            if code == 0 and os.path.exists(wav):
                wavs.append(wav)
        if len(wavs) == 2:
            same = sha(wavs[0]) == sha(wavs[1])
            result["audio"] = "identical" if same else "differs"
            result["deterministic"] = result["deterministic"] and same

    if by_design and not result["deterministic"]:
        result["deterministic"] = True # reported, not failed
        result["note"] = "differs, but the patch is listed in tools/determinism-by-design.txt"
    if args.json:
        print(json.dumps(result))
    else:
        verdict = "deterministic" if result["deterministic"] and not result.get("note") else (
            "nondeterministic_by_design" if by_design else "NOT deterministic")
        print(f"{args.patch}: {verdict} ({result['frames_compared']} frames, audio {result['audio']})")
        if result["first_differing_frame"]:
            print(f"  first differing frame: {result['first_differing_frame']}")
        if result["first_differing_node"] is not None:
            print(f"  first differing node: {result['first_differing_node']} (node index in the patch)")
    return 0 if result["deterministic"] else 1


if __name__ == "__main__":
    sys.exit(main())
