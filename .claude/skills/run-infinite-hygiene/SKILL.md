---
name: run-infinite-hygiene
description: Build, launch, and drive Infinite (the macOS node compositor) through its built-in self-test harness before committing/pushing — checks undo/redo, patch save/load, node groups, comments, color picker, macros, palette, bypass, geometry ops, 3D shading, materials, ocean/path, selection UI, audio param/teardown sweeps, and a full 167-node-type round trip. Use when asked to "run the tests", "hygiene check", "pre-commit check", "sanity check before pushing", or "verify the build" for this project.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`), not
this skill directory.

## Run this first

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

This builds the app, takes a rendered screenshot, then drives the compiled
`.app` binary through 43 self-test fixtures via env vars — real ImGui frames
and GL draws, not a mock. It prints `[pass]`/`[FAIL]`/`[CRASH]` per check and
exits non-zero if anything failed. Full raw output per check is saved to
`/tmp/infinite_test_<NAME>.log` so a failure can be read in full.

Flags:
- `--skip-build` — reuse the existing `build/` tree (fast iteration once you've built once)
- `--shot-only` — just build + screenshot, skip the 32-check suite (quick visual spot-check)

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
`FAIL`, or ending in `BUG`. `driver.sh` runs a curated 43 of these — the ones
with an unambiguous machine-checkable verdict — and greps for the failure
markers. See `ARCHITECTURE.md`'s "Dev/Test Harness" section for where this
code lives.

The 43 were picked to cover, category by category:

| Area | Checks |
|---|---|
| Core engine (undo, patch format) | UNDOTEST, PATCHTEST, ROUNDTRIPTEST (167 node types, copy/paste + save/load) |
| Editor UI | GROUPTEST, COMMENTTEST, HIDETEST, SELECTTEST, DRAGTEST |
| Per-node mini viewport | MINIVIEWPORTTEST (NodeViewport solo-renders a Select node's own mesh + selection overlay, independent of a plain untouched sibling) |
| Params / modulation / color | COLORTEST, MACROTEST, PALETTETEST, BYPASSTEST |
| Node math — geometry/mesh | GEOTEST, MESHOPTEST, TEXT3DTEST, FIXTEST, PHASEA/C/D/E/F |
| Real point distribution (Phase 6) | DISTRIBUTETEST — `MeshOps::DistributeOnFaces` covers the sphere's poles uniformly (unlike `ToPoints`' index-stride sampling) and is seed-reproducible; Poisson disk mode holds `minDistance` and saturates instead of packing tighter at high density; `MeshOps::MergeByDistance` is a no-op at threshold 0 and collapses seam vertices at a large one; `PointsToVerticesNode` trips `Mesh::Empty()` on purpose but reports `HasGeometry()`, carries colour, and drops dead particles; `DistributePointsInGridNode` produces the exact count in `ImageToPointsNode`'s row-major/cell-centre order |
| Node math — 3D render | 3DTEST, SHADOWTEST, MATFRAMETEST, MAPTEST, PATHOCEANTEST, ENVTEST |
| Regression fixtures | BUGTEST, LIVETEST |
| Point clouds as Render 3D sprites (Phase 1) | PHASE1TEST — Mesh to Points and Image to Points draw as clouds (not just meshes), a Particle System connects directly to Render 3D with no `IGeometrySource` on either side, and the render keeps advancing (`TextureRevision()` moves) instead of freezing on the animated cloud's first frame |
| One geometry interface, generic connect/disconnect/rebuild (Phase 2) | PATCHTEST also covers a second Render 3D geometry slot, both mesh-sampling pins plus the cloud pin on Instance on Points, Metaballs' cloud, and Path's curve pin surviving a save/load round trip; DELETECRASHTEST — one source feeds Render 3D, all three Instance on Points pins, Metaballs' cloud and both of Path's pins at once, gets deleted, and every consumer cooks without crashing on the freed pointer (the bug class `DisconnectAllTo`'s generic `GeometryInputSlot` loop removes structurally) |
| Geometry-node sweeps — generic across every `IGeometrySource`-consuming node type, not one hand-written fixture per node (see `geometry-transform-sweep` skill for the full writeup) | TRANSFORMSWEEPTEST (upstream `GetModelMatrix()` reaches the output), MAPPINGSWEEPTEST (upstream `GetMappingTransform()` reaches the output), REVISIONSWEEPTEST (a node's revision/generation stamp doesn't move when nothing actually changed — the class of bug that made Cloth reset to rest pose every frame downstream of a textured Displacement) |
| Selection as an input, not four operators (Phase 4) | PHASE4TEST — `GeometryOpNode::selectionOnly` moves/deletes/extrudes only a Select node's masked faces when on and the whole mesh when off; a patch saved before this phase (kDeleteSelected/kTransformSelected/kExtrudeSelected, no `selectionOnly` key at all) still loads to the same geometry after `MigrateDeprecatedOp` rewrites it to the general op + `selectionOnly=true` |
| Audio-node sweeps — generic across every audio/note node type registered in `NodeFactory`, not one hand-written fixture per node (see `audio-node-sweep` skill for the full writeup) | AUDIOPARAMSWEEPTEST (every `VisitParams` param survives save/load and reaches the audio thread within one block of its own `CookIfNeeded`; headless, runs before `glfwInit()`), AUDIOTEARDOWNSWEEPTEST (`DELETECRASHTEST` for the audio graph — spawn, wire, delete mid-playback via the real `RemoveNodeByIndex`, keep rendering, cables cleared) |

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
re-check one area after a fix, without the full 32-check sweep.

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
