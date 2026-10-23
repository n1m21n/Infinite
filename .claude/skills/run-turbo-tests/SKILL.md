---
name: run-turbo-tests
description: Build Infinite-Turbo (Windows) and drive Infinite-Turbo.exe through its built-in env-var self-test harness (undo, patch save/load, groups, colour, macros, palette, bypass, geometry/3D, audio param and teardown sweeps, 167-node round trip) plus a screenshot, writing results to build\test-results\. Use when asked to "run the tests", "hygiene check", "sanity check", "verify the build", after adding or changing a node, or before committing.
---

# run-turbo-tests

Windows replacement for the old macOS `run-infinite-hygiene` driver.

## How to run

From the repo root on the Windows machine (Palmi runs it, or Claude Code
running natively on Windows):

```bat
test-windows.bat                  :: build + screenshot + full suite
test-windows.bat -SkipBuild       :: reuse the current build
test-windows.bat -Quick           :: 6-check smoke subset (fast, after small edits)
test-windows.bat -Only AUDIOPARAMSWEEPTEST,AUDIOTEARDOWNSWEEPTEST   :: audio sweeps
test-windows.bat -Only TRANSFORMSWEEPTEST,MAPPINGSWEEPTEST,REVISIONSWEEPTEST,RENDER3DCACHESWEEPTEST :: geometry sweeps
test-windows.bat -Config Debug    :: test the Debug build
test-windows.bat -ShotOnly        :: build + screenshot only
test-windows.bat -Perf            :: suite + perf regression run (perf.ps1)
test-windows.bat -PerfOnly -SkipBuild :: perf run only
test-windows.bat -Compat          :: suite + patch corpus vs reference (compat.ps1)
test-windows.bat -Mit             :: MIT edition (build\windows-vs2022-mit, results in build\test-results-mit)
```

`test-windows.bat` forwards every argument to
`.claude\skills\run-turbo-tests\driver.ps1`.

## Reading the results (also from a Cowork session)

Everything lands inside the project folder, so a Cowork session linked to the
folder can read it with its shell after the user runs the tests:

| File | Content |
|---|---|
| `build\test-results\summary.txt` | one line per check, totals, failing list |
| `build\test-results\<NAME>.log` | full stdout+stderr of that check |
| `build\test-results\screenshot.png` | visual smoke render: open it and check that previews, text and chrome draw |
| `build\test-results\build.log` | build output when the driver built |

Markers: `[pass]`, `[FAIL]` (verdict line contains FAIL or ends in BUG),
`[CRASH]` (non-zero exit, hex code shown: 0xC0000005 = access violation),
`[HANG]` (killed after `-TimeoutSec`, default 180), `[EMPTY]` (no output: the
frame budget is too small or stdout was lost).

Before calling a FAIL a regression, compare with `known-baseline.md` next to
this file: many AUDIOPARAMSWEEPTEST params fail by design (no sample loaded,
gated by a sibling default, effect slower than the probe window).

## How it works

`src/main.cpp` contains ~50 self-tests gated by `getenv("INFINITE_<NAME>")`.
Each spawns a fixture graph, runs N frames (`INFINITE_EXITAFTER=N` closes the
window at frame N) and prints a verdict line. `main()` always returns 0, so
the verdict text is the only signal. The driver sets the two env vars, starts
the exe with stdout/stderr redirected to the log, waits, and greps.
Infinite-Turbo is a GUI-subsystem exe; redirected handles still receive
`printf` output, and the harness makes stdout unbuffered.

Single check by hand (cmd):

```bat
set INFINITE_UNDOTEST=1& set INFINITE_EXITAFTER=10& build\windows-vs2022\Release\Infinite-Turbo.exe > undo.log 2>&1
```

## Adding a check for a new node

1. Add the fixture in `src/main.cpp` next to the others (search
   `getenv("INFINITE_DSPTEST")` for audio, `SWEEPTEST` for geometry), printing a
   line ending in `OK` or containing `FAIL`.
2. Add `"<NAME>:<frames>"` to `$tests` in `driver.ps1`. Find the frame count by
   running the check alone and raising `INFINITE_EXITAFTER` until the verdict
   prints, then pad a few frames.
3. Prefer wiring the node into the generic sweeps (audio-node-sweep,
   geometry-transform-sweep) over a one-off fixture.

## Patch compatibility (two builds, many patches)

`patchcompat.ps1 -Exe <exe> -Corpus <dir of .inf> -Out <dir>` opens every patch with the
`INFINITE_PATCHCOMPAT` probe (src/app/selftest/PatchCompat.inl): per node its output size, a 4x4
thumbnail, triangle / vertex / point counts and modulator value, plus node types and saved keys the
build does not know. Run it with a reference build and a candidate, then
`python compare-patchcompat.py <ref out> <new out> report.md` lists regressions (ok -> crash /
hang), per-node differences, and the unknown types / keys over the whole corpus. Run the two builds
one after the other: two instances at once delay Text nodes past the probe frame.

## Windows-specific notes

- PLUGINSCANTEST (Audio Units) was dropped. MEDIADRAGTEST / SAMPLERDRAGTEST
  stage their own fixture file in C:\tmp. PLUGINDRAGTEST feeds the panel one
  fake row (no VST3 needed); `INFINITE_PLUGINDRAG_REALSCAN=1` runs a real scan,
  which can outlast the frame budget on a machine with many plugins.
- Close Infinite-Turbo before running: the build refuses while it is open.
- Screenshot resolution follows the fixture framebuffer and DPI scale.

## Profiling switches (Turbo 0.70)

- `INFINITE_FRAMEPROF=1`: CPU (and GPU) ms per frame stage, slowest node bodies. A body time can be
  the CPU waiting on a full GPU queue - check with GPUPROF before optimising it.
- `INFINITE_GPUPROF=1`: GPU ms per debug group (Render 3D, shadow, post, Preview 3D, each PointZ
  operator), every 120 frames.
- `INFINITE_GLDEBUG=1` / `=sync`: GL errors and performance warnings, with the frame number; `sync`
  names the debug group and the calling code line.
- Tracy: build with `-DINFINITE_ENABLE_TRACY=ON` and connect the Tracy v0.11.1 GUI.

## Unit tests (A7, Turbo 0.70)

Every run (unless `-Exe` points at a foreign build) builds the `InfiniteUnitTests` target
(doctest, `tests\unit\*.cpp`, not part of the normal build) and runs it as UNITTEST; log in
`build\test-results\UNITTEST.log`. Put code that needs no window and no GL context here
instead of a frame-driven self-test: add the .cpp under test to the target's source list in
CMakeLists.txt. A known gap is written as a case marked `doctest::may_fail(true)` with the item ID.

## Perf regression (F1, Turbo 0.70)

`perf.ps1` plays every patch in `perf-corpus.txt` (copied with its `_media` folder into
`build\perf\corpus`) with the patchcompat probe in play mode, `INFINITE_FRAMEPROF` and
`INFINITE_PERF_UNCAPPED` (VSync and frame limiter off), 3 runs, median per metric. Results:
`build\perf\results.json` and `build\perf\report.txt`.

The gate is an A/B in the same session: `-RefExe build\perf-ref\Infinite-Turbo.exe` runs the
reference and the candidate alternately, so clock and thermal drift hit both (the same build moved
~20% between sessions, twice the threshold). `-Perf` in the driver does this whenever
`build\perf-ref` exists. Fails when frameMs, cpu.total, gpu.total, render3d or a node GPU time
>= 0.5 ms is more than 10% and 0.5 ms worse; stages and node CPU times are reported only.

`build\perf-ref` is a frozen copy of a Release folder (robocopy /MIR). For turbo/next it is the M0
build (v0.62.0 + the `INFINITE_PERF_UNCAPPED` probe + F3 markers): v0.62.0 itself cannot run
uncapped. Refresh it only when the owner accepts a new cost level, and say so.
`perf-baseline.json` (`-UpdateBaseline`) is only a cross-session trend.

## Patch corpus gate (C3, Turbo 0.70)

`compat.ps1` stages `compat-corpus.txt` (files or folders, `_media` siblings included, numbered
names) into `build\compat\corpus`, runs `patchcompat.ps1`, then `compare-patchcompat.py --gate`
against `build\compat\ref`. Record the reference once with the reference build:
`compat.ps1 -RecordRef` (for turbo/next: the v0.62.0 build). Report: `build\compat\report.md`.

