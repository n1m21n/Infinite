#!/usr/bin/env python3
"""Infinite Benchmark Runner CLI (docs/plans/perf/benchmark-suite.md)

Usage:
  python3 tools/bench.py b9 [--scene b2|b4|all] [--frames N] [--app PATH]
  python3 tools/bench.py b11 [--scene a|b|c|all] [--n N] [--app PATH] [--json-out FILE]
  python3 tools/bench.py b2 [--scale s|m|l] [--anim 0|1] [--app PATH]
  python3 tools/bench.py b4 [--scale s|m|l] [--shadow off|1024|2048|4096] [--anim 0|1] [--app PATH]
  python3 tools/bench.py b1 [--buffer 64|128|256|512] [--seconds S] [--app PATH]
  python3 tools/bench.py b5 [--sub empty|nodecount|stages|startup|loadsave|undo] [--app PATH]
  python3 tools/bench.py all [--app PATH]
  python3 tools/bench.py compare <baseline.jsonl> <new.jsonl>
  python3 tools/bench.py quality <results.jsonl>... [--untrusted]

quality: print one `QUALITY id=bench.<metric> subject="bench:<B>:<variant>" value=<n>` row per
measured target (docs/quality/README.md, registry ids `bench.*`), for tools/quality/quality.py
check/ingest. Only trusted rows are printed: a row whose variant carries unfocused=1, unpaced=1 or
overlap=1 (scripts/bench/compare.py's marks), or a B6 vsync=0 row, is skipped with a note on stderr,
because an untrusted run can only look better than the real thing and must never count as met.
--untrusted prints them too, under subjects ending ":untrusted" (for reading, never for ingest).
`b9` prints its rows after the report. Exit: 0 ok, 1 no rows, 2 usage error.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts" / "bench"))
from compare import normalize_variant  # noqa: E402  one owner of the untrusted marks


def _kv(variant):
    return dict(p.split("=", 1) for p in str(variant or "").split(",") if "=" in p)


def _num(v):
    return v if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def bench_label(r, variant):
    """Short, stable subject label for one BENCH_JSON row: B<n>:<variant>."""
    b = str(r.get("bench", "")).split("_")[0]
    kv = _kv(variant)
    if b == "B1":
        v = "buf%s" % kv.get("buffer", "?")
    elif b in ("B3", "B7"):
        v = kv.get("scale", "s") + (":%smin" % kv["minutes"] if "minutes" in kv else "")
    elif b == "B6":
        v = "n%s-%s" % (kv.get("n", "?"), kv.get("mode", "all"))
        v += "".join("-" + k + ("" if kv[k] == "1" else kv[k]) for k in ("collapsed", "zoom", "delete") if k in kv)
    elif b == "B8":
        v = "%sx%s" % (kv.get("clips", "?"), kv.get("res", "?"))
        if kv.get("windows", "0") != "0":
            v += "+%sw" % kv["windows"]
        v += "+cam" if kv.get("camera") == "1" else ""
        v += "+syphon" if kv.get("syphon") == "1" else ""
    elif b in ("B2", "B4"):
        v = variant.replace(",gputimers=0", "")
    elif b == "B9":
        v = kv.get("scene", "?")
    elif b == "B11":
        v = "%s-%s" % (kv.get("scene", "?"), kv.get("n", "?"))
    elif b == "B10":
        v = "%ss-anim%s" % (kv.get("seconds", "?"), kv.get("anim", "1"))
    elif b == "B5":
        sub = str(r.get("bench", "")).split("_")[-1]
        v = sub + (":%s" % variant if variant else "")
    else:
        v = variant
    return "bench:%s:%s" % (b, v)


def quality_rows(r):
    """[(id, value)] for one BENCH_JSON row; the registry (docs/quality/registry.tsv) owns the targets."""
    b = str(r.get("bench", ""))
    out = []

    def add(mid, v):
        v = _num(v)
        if v is not None:
            out.append((mid, float(v)))

    audio = r.get("audio") or {}
    if b.startswith("B1_"):
        if _num(audio.get("cb_load_p99")) is not None:
            add("bench.b1_cb_load_p99", audio["cb_load_p99"] * 100.0)
        if audio.get("buffer") == 256:
            add("bench.b1_xruns", audio.get("xruns"))
    elif b.startswith("B3_") and r.get("edits"):
        # Edit storm variant (INFINITE_BENCH_B3EDITS): its own rows, so it never
        # overwrites the plain B3 projector numbers.
        e = r["edits"]
        if not e.get("failed"):
            add("bench.b3_edit_late_frames", max(_num(e.get("over_budget")) or 0, _num(e.get("late_work")) or 0))
            add("bench.b3_edit_work_ms_max", e.get("work_ms_max"))
            add("bench.b3_edit_xruns", audio.get("xruns"))
    elif b.startswith("B3_"):
        proj = r.get("projector") or {}
        add("bench.b3_projector_p99_ms", (proj.get("present_interval_ms") or {}).get("p99"))
        add("bench.b3_missed_vsync_pct", proj.get("missed_vsync_pct"))
        add("bench.b3_input_to_photon_frames", (r.get("input_to_photon_frames") or {}).get("max"))
    elif b.startswith("B6_"):
        nav = r.get("canvas_nav") or {}
        pan = nav.get("pan_frame_ms") or {}
        if (_num(r.get("nodes")) or 0) > 1000:
            # R5 (4000 nodes, run_all.sh --r5): its own report rows; the B6 pan
            # targets are for a 200-400 node patch.
            if _num(pan.get("p50")):
                add("bench.r5_pan_p95_ms", pan.get("p95"))
            sel = nav.get("select_frame_ms") or {}
            if _num(sel.get("p50")):
                add("bench.r5_select_p95_ms", sel.get("p95"))
            if _num(nav.get("delete_all_frame_ms")) is not None:
                add("bench.r5_delete_all_ms", nav["delete_all_frame_ms"])
                add("bench.r5_nodes_after_delete", nav.get("nodes_after_delete"))
        elif _num(pan.get("p50")):  # 0 = no pan phase in this mode
            add("bench.b6_pan_p50_ms", pan.get("p50"))
            add("bench.b6_pan_p95_ms", pan.get("p95"))
    elif b.startswith("B7_"):
        soak = r.get("soak") or {}
        add("bench.b7_rss_growth_pct", soak.get("rss_growth_pct"))
        add("bench.b7_xruns", soak.get("xruns_total"))
        add("bench.b7_thermal_fps_drop_pct", soak.get("thermal_fps_drop_pct"))
    elif b.startswith("B8_"):
        m = r.get("media_io") or {}
        for i, c in enumerate(m.get("clips") or []):
            if _num(c.get("decoded_fps")) is not None and _num(c.get("fps")):
                out.append(("bench.b8_decode_rate_ratio", c["decoded_fps"] / c["fps"], ":clip%d" % i))
            if _num(c.get("dropped")) is not None:
                out.append(("bench.b8_dropped_frames", float(c["dropped"]), ":clip%d" % i))
        for i, w in enumerate(m.get("windows") or []):
            if _num((w.get("interval_ms") or {}).get("p99")) is not None:
                out.append(("bench.b8_window_p99_ms", w["interval_ms"]["p99"], ":w%d" % i))
            if _num(w.get("missed_vsync_frac")) is not None:
                out.append(("bench.b8_window_missed_vsync_pct", w["missed_vsync_frac"] * 100.0, ":w%d" % i))
    elif b.startswith("B9_"):
        add("bench.b9_footprint_peak_mb", (r.get("mem") or {}).get("footprint_peak_mb"))
    elif b.startswith("B10_"):
        o = r.get("offline_render") or {}
        add("bench.b10_realtime_factor", o.get("realtime_factor"))
        add("bench.b10_av_drift_worst_frames", abs(o["drift_worst_frames"]) if _num(o.get("drift_worst_frames")) is not None else None)
        add("bench.b10_av_drift_end_frames", abs(o["drift_end_frames"]) if _num(o.get("drift_end_frames")) is not None else None)
    elif b == "B5_fundamentals_startup":
        add("bench.b5_startup_ms", (r.get("stages_cpu_ms") or {}).get("total_to_first_frame"))
    elif b == "B5_fundamentals_loadsave":
        st = r.get("stages_cpu_ms") or {}
        add("bench.b5_load_ms", st.get("load"))
        add("bench.b5_save_ms", st.get("save"))
    add("bench.fbo_allocs_steady", r.get("fbo_allocs_steady"))  # B2/B4 (and B9 when it reports it)
    return [x if len(x) == 3 else (x[0], x[1], "") for x in out]


def trusted(r):
    """(trusted, normalized variant). Untrusted = compare.py's marks, or B6 with vsync off (its frame
    times are pure work, a lower bound, and the fixture itself leaves its verdicts null)."""
    variant, untrusted = normalize_variant(r.get("variant"))
    if str(r.get("bench", "")).startswith("B6_") and _kv(variant).get("vsync") == "0":
        untrusted = True
    return not untrusted, variant


def quality_lines(results, include_untrusted=False, log=None):
    lines = []
    for r in results:
        ok, variant = trusted(r)
        if not ok and not include_untrusted:
            if log:
                log("bench quality: skipped untrusted %s [%s]" % (r.get("bench"), r.get("variant")))
            continue
        label = bench_label(r, variant) + ("" if ok else ":untrusted")
        for mid, v, suffix in quality_rows(r):
            lines.append('QUALITY id=%s subject="%s" value=%.4f' % (mid, (label + suffix).replace('"', "'"), v))
    return lines


def read_jsonl(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("BENCH_JSON "):
                line = line[len("BENCH_JSON "):]
            if line.startswith("{"):
                rows.append(json.loads(line))
    return rows


def cmd_quality(args):
    results = []
    for p in args.results:
        results += read_jsonl(p)
    lines = quality_lines(results, args.untrusted, log=lambda m: print(m, file=sys.stderr))
    for line in lines:
        print(line)
    return 0 if lines else 1


def default_app_path():
    repo_root = Path(__file__).resolve().parent.parent
    app_mac = repo_root / "build" / "Infinite.app" / "Contents" / "MacOS" / "Infinite"
    app_mac_bin = repo_root / "build" / "bin" / "Infinite.app" / "Contents" / "MacOS" / "Infinite"
    app_direct = repo_root / "build" / "Infinite"
    if app_mac.exists():
        return str(app_mac)
    if app_mac_bin.exists():
        return str(app_mac_bin)
    if app_direct.exists():
        return str(app_direct)
    return str(app_mac)


def run_process_for_json(app_path, env_vars, exitafter=160):
    env = os.environ.copy()
    env.update(env_vars)
    env["INFINITE_EXITAFTER"] = str(exitafter)

    proc = subprocess.run([app_path], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    results = []
    for line in proc.stdout.splitlines():
        if line.startswith("BENCH_JSON "):
            try:
                results.append(json.loads(line[len("BENCH_JSON "):].strip()))
            except Exception as e:
                print(f"Failed to parse JSON: {e}", file=sys.stderr)
    return results, proc.stdout, proc.stderr, proc.returncode


def print_b9_report(r):
    bench = r.get("bench", "B9_memory_footprint")
    variant = r.get("variant", "")
    frames = r.get("frames", 0)
    mem = r.get("mem", {})
    frame_ms = r.get("frame_ms", {})
    stages_cpu = r.get("stages_cpu_ms", {})
    tris = r.get("tris", 0)
    draw_calls = r.get("draw_calls")
    hash_val = r.get("output_hash", "n/a")

    print("\n" + "=" * 80)
    print(f"BENCHMARK: {bench} [{variant}] (frames={frames})")
    print("=" * 80)

    print("\n[Host & Configuration]")
    print(f"  Machine:     {r.get('machine', 'unknown')}")
    print(f"  GPU:         {r.get('gpu', 'unknown')}")
    print(f"  Commit:      {r.get('commit', 'unknown')}")
    print(f"  Nodes:       {r.get('nodes', 0)}")
    print(f"  Triangles:   {tris:,}")
    if draw_calls is not None:
        print(f"  Draw Calls:  {draw_calls}")
    print(f"  Output Hash: {hash_val}")

    print("\n[Host Memory Progression]")
    print(f"  Start (f2):        RSS {mem.get('rss_mb_start', -1):.2f} MB | Phys {mem.get('phys_mb_start', -1):.2f} MB")
    if 'rss_built_mb' in mem or 'phys_built_mb' in mem:
        print(f"  Post-Build:        RSS {mem.get('rss_built_mb', -1):.2f} MB | Phys {mem.get('phys_built_mb', -1):.2f} MB")
    if 'rss_f32_mb' in mem or 'phys_f32_mb' in mem:
        print(f"  Frame 32 (warmup): RSS {mem.get('rss_f32_mb', -1):.2f} MB | Phys {mem.get('phys_f32_mb', -1):.2f} MB")
    if 'rss_f152_mb' in mem or 'phys_f152_mb' in mem:
        print(f"  Frame 152:         RSS {mem.get('rss_f152_mb', -1):.2f} MB | Phys {mem.get('phys_f152_mb', -1):.2f} MB")
    print(f"  End (f{frames}):       RSS {mem.get('rss_mb', -1):.2f} MB | Phys {mem.get('phys_mb', -1):.2f} MB")
    if 'rss_peak_mb' in mem or 'phys_peak_mb' in mem:
        print(f"  Peak Footprint:    RSS {mem.get('rss_peak_mb', -1):.2f} MB | Phys {mem.get('phys_peak_mb', -1):.2f} MB")
    if 'phys_slope_mb_per_100f' in mem or 'rss_slope_mb_per_100f' in mem:
        p_slope = mem.get('phys_slope_mb_per_100f', 0.0)
        r_slope = mem.get('rss_slope_mb_per_100f', 0.0)
        p_status = "OK (stable)" if abs(p_slope) < 0.5 else ("WARN (growing)" if p_slope > 0 else "DECREASING")
        print(f"  Phys Growth Slope: {p_slope:+.4f} MB / 100 frames  [{p_status}]")
        print(f"  RSS Growth Slope:  {r_slope:+.4f} MB / 100 frames")

    if 'gpu_est_mb' in mem:
        print("\n[Estimated GPU Memory Breakdown]")
        print(f"  Total Estimated:   {mem.get('gpu_est_mb', 0):.2f} MB")
        bd = mem.get('gpu_est_breakdown', {})
        if bd:
            print(f"    Textures:        {bd.get('textures_mb', 0):.2f} MB")
            print(f"    Render Targets:  {bd.get('render_targets_mb', 0):.2f} MB")
            print(f"    Shadow Maps:     {bd.get('shadow_maps_mb', 0):.2f} MB")
            print(f"    Mesh Buffers:    {bd.get('mesh_buffers_mb', 0):.2f} MB")
            print(f"    Instance Buffers:{bd.get('instance_buffers_mb', 0):.2f} MB")

    if frame_ms:
        print("\n[Frame Timing]")
        print(f"  p50:  {frame_ms.get('p50', 0):.3f} ms ({1000.0/max(0.001, frame_ms.get('p50', 1)):.1f} fps)")
        print(f"  p95:  {frame_ms.get('p95', 0):.3f} ms")
        print(f"  p99:  {frame_ms.get('p99', 0):.3f} ms")
        print(f"  max:  {frame_ms.get('max', 0):.3f} ms")

    if stages_cpu:
        print("\n[CPU Stages (p50)]")
        for stage, ms in stages_cpu.items():
            print(f"  {stage:<16} {ms:.3f} ms")
    print("=" * 80 + "\n")


def cmd_b9(args):
    app = args.app or default_app_path()
    if not os.path.exists(app):
        print(f"Error: binary '{app}' does not exist. Build first via: cmake --build build -j", file=sys.stderr)
        return 1

    scenes = ["b2", "b4"] if args.scene == "all" else [args.scene]
    frames = args.frames
    exit_after = frames + 50

    all_results = []
    for s in scenes:
        print(f"Running B9 Memory Benchmark (scene={s}, scale=l, anim=1, frames={frames})...")
        env_vars = {
            "INFINITE_BENCH_B9SCENE": s,
            "INFINITE_BENCH_B9FRAMES": str(frames),
        }
        results, stdout, stderr, code = run_process_for_json(app, env_vars, exitafter=exit_after)
        if not results:
            print(f"Failed: No BENCH_JSON produced. Process output:\n{stdout}\n{stderr}", file=sys.stderr)
            return 1
        for r in results:
            print_b9_report(r)
            all_results.append(r)
        for line in quality_lines(results, log=lambda m: print(m, file=sys.stderr)):
            print(line)

    if args.json_out:
        with open(args.json_out, "w") as f:
            for r in all_results:
                f.write(json.dumps(r) + "\n")
        print(f"Saved results to {args.json_out}")

    return 0


# B11 points (P8 phase 0, docs/plans/perf/benchmark-suite.md): the plan's sizes per scene.
# Scene a's Image to Points grid is square and capped at 512 (262,144 points), so its
# 920k row is clamped by the node and reported as such.
B11_MATRIX = {
    "a": [57600, 230400, 921600],
    "b": [10000, 100000, 200000],
    "c": [10000, 100000],
}


def cmd_b11(args):
    app = args.app or default_app_path()
    if not os.path.exists(app):
        print(f"Error: binary '{app}' does not exist. Build first via: scripts/ibuild.py build", file=sys.stderr)
        return 1
    scenes = ["a", "b", "c"] if args.scene == "all" else [args.scene]
    rows = []
    for s in scenes:
        for n in ([args.n] if args.n else B11_MATRIX[s]):
            print(f"Running B11 points scene={s} n={n} ...", file=sys.stderr)
            env = {"INFINITE_BENCH_B11": s, "INFINITE_BENCH_B11N": str(n)}
            if s == "a":
                env["INFINITE_BENCH_B8MEDIA"] = str(Path(__file__).resolve().parent.parent / "bench" / "media")
            try:
                results, out, err, code = run_process_for_json(app, env, exitafter=100000)
            except Exception as e:  # noqa: BLE001  a crash in one row must not lose the others
                print(f"  failed: {e}", file=sys.stderr)
                continue
            if not results:
                print(f"  no BENCH_JSON (exit {code}):\n{err[-2000:]}", file=sys.stderr)
                continue
            rows += results
    print(f"{'scene':<6}{'asked':>9}{'points':>9}{'filled':>8}{'p50 ms':>9}{'p99 ms':>9}{'fps p50':>9}{'cook ms':>9}{'tris':>11}")
    for r in rows:
        kv = _kv(r.get("variant"))
        pts = r.get("points") or {}
        fm = r.get("frame_ms") or {}
        p50 = fm.get("p50", 0) or 0
        print(f"{kv.get('scene', '?'):<6}{pts.get('asked', 0):>9}{pts.get('count', 0):>9}{str(pts.get('filled')):>8}"
              f"{p50:>9.2f}{fm.get('p99', 0):>9.2f}{1000.0 / max(p50, 0.001):>9.1f}"
              f"{(r.get('stages_cpu_ms') or {}).get('cook', 0):>9.2f}{r.get('tris', 0):>11,}")
    if args.json_out and rows:
        with open(args.json_out, "w") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")
        print(f"Saved results to {args.json_out}", file=sys.stderr)
    return 0 if rows else 1


def main():
    parser = argparse.ArgumentParser(description="Infinite Performance Benchmark Suite CLI")
    subparsers = parser.add_subparsers(dest="command")

    # B9
    p_b9 = subparsers.add_parser("b9", help="Run B9 (Memory footprint)")
    p_b9.add_argument("--scene", choices=["b2", "b4", "all"], default="b2", help="Scene to run (default: b2)")
    p_b9.add_argument("--frames", type=int, default=600, help="Total frames to run (default: 600)")
    p_b9.add_argument("--app", type=str, default=None, help="Path to Infinite executable")
    p_b9.add_argument("--json-out", type=str, default=None, help="File to write jsonl results")
    p_b9.set_defaults(func=cmd_b9)

    # B11
    p_b11 = subparsers.add_parser("b11", help="Run B11 (points: P8 phase 0)")
    p_b11.add_argument("--scene", choices=["a", "b", "c", "all"], default="all")
    p_b11.add_argument("--n", type=int, default=0, help="One point count instead of the plan's sizes")
    p_b11.add_argument("--app", type=str, default=None, help="Path to Infinite executable")
    p_b11.add_argument("--json-out", type=str, default=None, help="File to write jsonl results")
    p_b11.set_defaults(func=cmd_b11)

    # Compare
    p_cmp = subparsers.add_parser("compare", help="Compare two benchmark result files")
    p_cmp.add_argument("baseline", help="Path to baseline jsonl")
    p_cmp.add_argument("new", help="Path to new jsonl")

    def cmd_cmp(args):
        compare_script = Path(__file__).resolve().parent.parent / "scripts" / "bench" / "compare.py"
        return subprocess.run([sys.executable, str(compare_script), args.baseline, args.new]).returncode

    p_cmp.set_defaults(func=cmd_cmp)

    # Quality rows
    p_q = subparsers.add_parser("quality", help="Print QUALITY rows (docs/quality) from BENCH_JSON results")
    p_q.add_argument("results", nargs="+", help="BENCH_JSON .jsonl files (bench/results/..., bench/baselines/...)")
    p_q.add_argument("--untrusted", action="store_true", help="also print untrusted rows (subject suffix :untrusted; never ingest these)")
    p_q.set_defaults(func=cmd_quality)

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        return 1

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
