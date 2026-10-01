#!/usr/bin/env python3
"""Golden-image / golden-audio check for the reference patches in tests/render/.

    python3 tools/render-check.py                 # compare every patch against tests/render/golden/
    python3 tools/render-check.py --update        # (re)write the goldens from this machine's render
    python3 tools/render-check.py compositing     # one patch
    python3 tools/render-check.py --binary build-linux/Infinite

Images: `Infinite --frame` at t=0 and t=1, decoded and compared per pixel. A frame passes when its
mean absolute difference is <= --mean-tol (0..255 per channel) and fewer than --bad-frac of the
pixels differ by more than --pixel-tol. That tolerance is what lets one golden set serve GPUs and
software rasterisers that differ in the last bit. Exact same-machine determinism is
tools/determinism-check.py, not this.

Audio (patches with an Audio Out): `--audio-summary`, compared on loudness (LUFS, peak, RMS) within
--db-tol dB. Not sample-exact: the goldens are a fingerprint, not a recording.

Exit 0 = all match, 1 = a mismatch (named), 2 = usage / tool failure.
"""

import argparse
import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES = os.path.join(ROOT, "tests", "render")
GOLDEN = os.path.join(PATCHES, "golden")
TIMES = "0,1"
DEFAULT_BINARY = {
    "darwin": "build/Infinite.app/Contents/MacOS/Infinite",
    "win32": "build/Release/Infinite.exe",
}.get(sys.platform, "build/Infinite")
AUDIO_KEYS = ("integrated_lufs", "sample_peak_dbfs", "true_peak_dbtp")


def read_png(path):
    """8-bit RGB/RGBA, non-interlaced PNG -> (w, h, channels, bytes of unfiltered rows)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: need 8-bit RGB/RGBA non-interlaced")
            ch = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev = w * ch, [], bytearray(w * ch)
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b, c = prev[i], (prev[i - ch] if i >= ch else 0)
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, ch, b"".join(rows)


def compare_png(got, want, pixel_tol, mean_tol, bad_frac):
    a, b = read_png(got), read_png(want)
    if a[:3] != b[:3]:
        return False, f"size/format differs: {a[:3]} vs {b[:3]}"
    total = bad = 0
    for x, y in zip(a[3], b[3]):
        d = abs(x - y)
        total += d
        bad += d > pixel_tol
    n = len(a[3])
    mean, frac = total / n, bad / n
    return mean <= mean_tol and frac <= bad_frac, f"mean diff {mean:.3f}/255, {frac * 100:.2f}% of samples over {pixel_tol}"


def run(binary, args, timeout):
    p = subprocess.run([binary] + args, capture_output=True, text=True, timeout=timeout,
                       env=dict(os.environ, INFINITE_NO_UPDATE_CHECK="1"))
    try:
        status = json.loads((p.stdout.strip().splitlines() or ["{}"])[-1])
    except ValueError:
        status = {}
    return p.returncode, status


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="patch names (default: all in tests/render)")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--binary", default=DEFAULT_BINARY)
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--pixel-tol", type=int, default=8)
    ap.add_argument("--mean-tol", type=float, default=1.0)
    ap.add_argument("--bad-frac", type=float, default=0.01)
    ap.add_argument("--db-tol", type=float, default=1.0)
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        print(f"error: no Infinite binary at {args.binary} (use --binary)", file=sys.stderr)
        return 2
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.inf")))
    if args.names:
        patches = [p for p in patches if os.path.splitext(os.path.basename(p))[0] in args.names]
    if not patches:
        print("error: no patches matched", file=sys.stderr)
        return 2
    os.makedirs(GOLDEN, exist_ok=True)

    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for patch in patches:
            name = os.path.splitext(os.path.basename(patch))[0]
            problems = []
            d = os.path.join(tmp, name)
            code, status = run(args.binary, ["--frame", patch, TIMES, d, "--lenient"], args.timeout)
            files = sorted(status.get("files", [])) if code == 0 else []
            if not files:
                print(f"FAIL {name}: --frame failed (exit {code}) {json.dumps(status.get('errors', ''))}")
                failed += 1
                continue
            for i, f in enumerate(files):
                gold = os.path.join(GOLDEN, f"{name}_{i}.png")
                if args.update:
                    shutil.copyfile(f, gold)
                elif not os.path.exists(gold):
                    problems.append(f"no golden {os.path.basename(gold)} (run with --update)")
                else:
                    ok, why = compare_png(f, gold, args.pixel_tol, args.mean_tol, args.bad_frac)
                    if not ok:
                        problems.append(f"frame {i}: {why}")

            code, status = run(args.binary, ["--audio-summary", patch, os.path.join(tmp, name + ".json"),
                                             "--duration", "2", "--lenient"], args.timeout)
            summary = status.get("audio_summary") if code == 0 else None
            gold = os.path.join(GOLDEN, f"{name}_audio.json")
            if summary:
                fp = {k: summary[k] for k in AUDIO_KEYS if k in summary}
                if args.update:
                    with open(gold, "w") as f:
                        json.dump(fp, f, indent=1)
                        f.write("\n")
                elif os.path.exists(gold):
                    want = json.load(open(gold))
                    for k, v in want.items():
                        if abs(fp.get(k, -999) - v) > args.db_tol:
                            problems.append(f"audio {k}: {fp.get(k)} vs golden {v} (tol {args.db_tol} dB)")
                else:
                    problems.append(f"no golden {os.path.basename(gold)} (run with --update)")

            if problems:
                failed += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"{'updated' if args.update else 'ok'}   {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
