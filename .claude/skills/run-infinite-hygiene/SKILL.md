---
name: run-infinite-hygiene
description: Build, launch, and drive Infinite (the macOS node compositor) through its built-in self-test harness before committing/pushing — checks undo/redo, patch save/load, node groups, comments, color picker, macros, palette, bypass, geometry ops, 3D shading, materials, ocean/path, selection UI, and a full 163-node-type round trip. Use when asked to "run the tests", "hygiene check", "pre-commit check", "sanity check before pushing", or "verify the build" for this project.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`), not
this skill directory.

## Run this first

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

This builds the app, takes a rendered screenshot, then drives the compiled
`.app` binary through 30 self-test fixtures via env vars — real ImGui frames
and GL draws, not a mock. It prints `[pass]`/`[FAIL]`/`[CRASH]` per check and
exits non-zero if anything failed. Full raw output per check is saved to
`/tmp/infinite_test_<NAME>.log` so a failure can be read in full.

Flags:
- `--skip-build` — reuse the existing `build/` tree (fast iteration once you've built once)
- `--shot-only` — just build + screenshot, skip the 30-check suite (quick visual spot-check)

Read the screenshot it writes (`/tmp/infinite_hygiene_shot.png`) with the
Read tool to eyeball that node previews, chrome, and text are actually
rendering — the suite's pass/fail lines don't catch "renders blank" or
"renders garbled" on their own.

## Why this exists / how it works

`src/main.cpp` already contains ~50 env-var-gated self-tests (search
`getenv("INFINITE_`) — a dev/test harness baked into the app itself, not a
separate test binary. Each one spawns a small fixture node graph, steps it
for N frames (`INFINITE_EXITAFTER=N` closes the GLFW window and flushes
stdout at frame N), and `printf`s a verdict line ending in `OK`, containing
`FAIL`, or ending in `BUG`. `driver.sh` runs a curated 30 of these — the ones
with an unambiguous machine-checkable verdict — and greps for the failure
markers. See `ARCHITECTURE.md`'s "Dev/Test Harness" section for where this
code lives.

The 30 were picked to cover, category by category:

| Area | Checks |
|---|---|
| Core engine (undo, patch format) | UNDOTEST, PATCHTEST, ROUNDTRIPTEST (163 node types, copy/paste + save/load) |
| Editor UI | GROUPTEST, COMMENTTEST, HIDETEST, SELECTTEST, SELECTVIZTEST, DRAGTEST |
| Params / modulation / color | COLORTEST, MACROTEST, PALETTETEST, BYPASSTEST |
| Node math — geometry/mesh | GEOTEST, MESHOPTEST, TEXT3DTEST, FIXTEST, PHASEA/C/D/E/F |
| Node math — 3D render | 3DTEST, SHADOWTEST, MATFRAMETEST, MAPTEST, PATHOCEANTEST |
| Regression fixtures | BUGTEST, LIVETEST |

Excluded from the auto-verdict suite because they only `printf` raw numbers
with no pass/fail line (would need a human to eyeball the log) — run them
manually if you touch that area:
`MODTEST`, `RECTEST`, `INPUTTEST`, `SIZETEST`, `TEXTFIT`, `RESYNTHTEST`,
`PICKERTEST` (needs a real mouse click, not simulatable headlessly).
Also excluded: `AUDIORECTEST`, `MODELTEST`, `INFINITE_BUILDSAMPLE*` — they
require an external audio/model file path as input.

## Build

```bash
cmake --build build -j8
```

`build/` already exists as a configured Debug-ish tree; `driver.sh` reuses it
if present and configures fresh only if missing. For the full universal
Release + DMG (what `package.sh` does for distribution), don't run that as
part of a pre-commit check — it's a from-scratch two-architecture rebuild and
takes minutes, not seconds.

## Direct invocation (single check)

```bash
INFINITE_UNDOTEST=1 INFINITE_EXITAFTER=10 ./build/Infinite.app/Contents/MacOS/Infinite
```

Any `INFINITE_<NAME>TEST=1` from the table above works the same way — set it,
give `INFINITE_EXITAFTER` enough frames (driver.sh's `TESTS` array has the
verified frame count for each), read stdout. This is the fastest way to
re-check one area after a fix, without the full 30-check sweep.

## Run (human path)

```bash
open build/Infinite.app
```

Opens the actual editor window, no env vars. Useless for a scripted check —
it just launches normally and waits for you to click around.

## Gotchas

- **Verdict lines aren't exit codes.** `main()` always `return 0`s regardless
  of test outcome — the harness's only pass/fail signal is the printf text.
  `driver.sh` greps for it; don't rely on `$?` from a raw
  `INFINITE_*TEST=1 ./Infinite` invocation.
- **Some tests need more frames than you'd guess.** The fixture graph has to
  build and settle before the check fires (e.g. `COLORTEST` clicks the color
  picker at frame 7 and reads the result at frame 11 — `EXITAFTER` under 13
  truncates before the verdict prints and the log is silently empty). If you
  add a check with a new `INFINITE_*` var, verify its frame count by running
  it directly and bumping `EXITAFTER` until the `OK`/`FAIL` line shows up,
  rather than guessing.
- **`INFINITE_MAPTEST` is about material maps** (roughness/normal/AO), not
  the minimap — the minimap has no self-test.
- Screenshot writes at whatever the fixture's framebuffer size is (2880x1472
  on this Retina display) — don't assume a fixed resolution when reading it.
