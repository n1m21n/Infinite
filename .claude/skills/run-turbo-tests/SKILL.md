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

## Windows-specific notes

- PLUGINSCANTEST (Audio Units) was dropped. PLUGINDRAGTEST / MEDIADRAGTEST /
  SAMPLERDRAGTEST need entries in the library panel (VST3 plugins, media,
  samples); on an empty library they time out and FAIL.
- Close Infinite-Turbo before running: the build refuses while it is open.
- Screenshot resolution follows the fixture framebuffer and DPI scale.
