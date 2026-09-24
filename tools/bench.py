#!/usr/bin/env python3
"""Infinite Benchmark Runner CLI (docs/plans/perf/benchmark-suite.md)

Usage:
  python3 tools/bench.py b9 [--scene b2|b4|all] [--frames N] [--app PATH]
  python3 tools/bench.py b2 [--scale s|m|l] [--anim 0|1] [--app PATH]
  python3 tools/bench.py b4 [--scale s|m|l] [--shadow off|1024|2048|4096] [--anim 0|1] [--app PATH]
  python3 tools/bench.py b1 [--buffer 64|128|256|512] [--seconds S] [--app PATH]
  python3 tools/bench.py b5 [--sub empty|nodecount|stages|startup|loadsave|undo] [--app PATH]
  python3 tools/bench.py all [--app PATH]
  python3 tools/bench.py compare <baseline.jsonl> <new.jsonl>
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


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

    if args.json_out:
        with open(args.json_out, "w") as f:
            for r in all_results:
                f.write(json.dumps(r) + "\n")
        print(f"Saved results to {args.json_out}")

    return 0


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

    # Compare
    p_cmp = subparsers.add_parser("compare", help="Compare two benchmark result files")
    p_cmp.add_argument("baseline", help="Path to baseline jsonl")
    p_cmp.add_argument("new", help="Path to new jsonl")

    def cmd_cmp(args):
        compare_script = Path(__file__).resolve().parent.parent / "scripts" / "bench" / "compare.py"
        return subprocess.run([sys.executable, str(compare_script), args.baseline, args.new]).returncode

    p_cmp.set_defaults(func=cmd_cmp)

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        return 1

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
