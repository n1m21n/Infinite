# Test Suite Tiering — measured plan

Supersedes the diagnostics in the "Test Suite Scalability & Performance
Optimization Plan". That document's phases are mostly sound as *engineering*,
but its cost model was estimated rather than measured, and the estimates are
wrong in ways that change the priority order. Everything below is measured on
this machine, Debug build, 2026-08-30.

## 1. Measured baseline

Full suite, 62 checks (the plan says 63), `--skip-build`: **135.2 s**.

| Item | Measured | Share |
|---|---|---|
| `VIDEOAUDIOTEST` | 62.9 s | 47 % |
| Process startup floor (62 × ~0.50 s) | ~31 s | 23 % |
| `VIDEOSPEEDTEST` | 5.9 s | 4 % |
| `AUDIORECOVERYTEST` | 5.1 s | 4 % |
| `AUDIOPARAMSWEEPTEST` | 2.9 s | 2 % |
| `PLUGINDRAGTEST` | 2.8 s | 2 % |
| `WTDRAGTEST` | 2.0 s | 1 % |
| everything else (55 checks) | ~23 s | 17 % |

Plus **63 s** to recompile after touching `src/main.cpp` (48,667 lines, one
translation unit). The original plan does not mention build cost at all, yet
it is a third of the real edit→commit cycle (~200 s total).

Frame cost with VSync on: **18.3 ms/frame** (measured: bare `INFINITE_EXITAFTER`
at 1/10/60/120 frames → 0.52/0.66/1.56/2.68 s). Startup+teardown floor for a
GL fixture: **~0.50 s**. For a pre-`glfwInit` fixture: **~0.06 s**.

### 1.1 `VIDEOAUDIOTEST` — the actual bottleneck

All 61 s of it are inside a single call. `INFINITE_EXITAFTER` of 3, 10, and 30
each complete in 1 s; 62 takes 62 s. Frame 62 is
`out->StopRecording()` ([src/main.cpp:41416](../../src/main.cpp#L41416)), which
blocks until AVAssetWriter finalizes. Finalizing a 1.23 s movie should not take
a minute — and the same run reports `saved 37 frames (19 dropped)`, i.e. the
recorder drops a third of its frames. **Treat this as a recorder bug to
investigate, not a test to speed up.** It is worth more than every other
optimization in the plan combined.

### 1.2 Claims in the original plan that did not hold

- *"~200 ms cold boot each"* → actually ~500 ms; the plan understates process
  overhead by 2.5×, which makes Phase 3 look less valuable than it is.
- *"VSync … ~58 s idle wait over 3,500+ frames"* → VSync is real at 18.3 ms/frame,
  but the 3,500 figure sums the *frame budgets*, not frames actually run. Most
  fixtures exit as soon as they print a verdict. Realistic saving: **~15–20 s**,
  not 58 s.
- *"`SAMPLERDRAGTEST:600`, `MEDIADRAGTEST:600`, `PLUGINDRAGTEST:600` drop from
  ~35 s to ~200 ms"* → measured 0.55 s, 0.53 s, 2.78 s. They already exit early.
  The proposed 600→20 budget cut saves nothing and risks truncating a fixture
  before its verdict, which `driver.sh` correctly reports as a hard failure
  ("no verdict printed"). **Do not do this.**
- *"Host VST3 walk → multi-minute hangs"* → `PLUGINDRAGTEST` is 2.78 s here.
  The hang may be real on a machine with many installed plugins; it was not
  reproduced. Isolate the scanner anyway — for **determinism across machines**,
  which is the honest justification — but do not count it as a time win.
- *Phase 2 "15 tests finish in < 300 ms total"* → those 15 already return before
  `glfwInit` ([src/main.cpp:33978–34050](../../src/main.cpp#L33978)) and cost
  0.06–2.87 s each. `AUDIOPARAMSWEEPTEST` alone is 2.87 s of real computation
  that batching cannot remove. True saving is the process floor only: **~3–4 s**.
  Low value for the work involved; do it last, or never.

### 1.3 Claims that did hold

- `glfwSwapInterval(1)` is unconditional at
  [src/main.cpp:34202](../../src/main.cpp#L34202), and `gHeadlessTestWindow`
  already exists at [34114](../../src/main.cpp#L34114) with branches at 34124 /
  34148 to hook into. The fix is three lines.
- `RemoteControl::Start(7777)` at
  [src/main.cpp:34337](../../src/main.cpp#L34337) is unconditional — bound and
  torn down 62 times per suite run. Small win, near-zero risk, do it.
- `UpdateCheck::Start()` at [34263](../../src/main.cpp#L34263) is already gated.

### 1.4 Phase 3 (in-process GL harness) — the risk

It targets the ~31 s of process floor, the second-largest item, so it is
correctly identified. But it trades away per-fixture process isolation, and
that isolation is doing real work in this harness: `driver.sh` reports a
non-zero exit as `[CRASH] <name>`, which is how `DELETECRASHTEST` and
`AUDIOTEARDOWNSWEEPTEST` report at all. One crash in a shared-process runner
takes out every fixture after it and loses attribution; leaked global state
between fixtures produces failures that do not reproduce standalone.

Recommendation: **do not build it until tiering has landed and proven
insufficient.** After fixing `VIDEOAUDIOTEST` and VSync the full suite is
~50 s, and the tiers below mean you rarely pay it. Phase 3 buys ~25 s more at
the cost of the harness's crash-attribution property.

The plan's "< 30 s full suite" target is unreachable without Phase 3 — 62
process launches cost 31 s on their own.

## 2. Priority order (revised)

| # | Change | Saving | Risk |
|---|---|---|---|
| 1 | Fix / bound `StopRecording()` in `VIDEOAUDIOTEST` (recorder bug) | ~61 s | — investigation |
| 2 | Tiering (§3) — stop running the full suite per commit | ~130 s per commit | low |
| 3 | `glfwSwapInterval(0)` under `gHeadlessTestWindow` | ~15–20 s | very low |
| 4 | Skip `RemoteControl::Start()` in test modes | ~1 s | very low |
| 5 | Synthetic fixture dir for `PLUGINDRAGTEST` | determinism, ~2 s | low |
| 6 | Bound `AUDIORECOVERYTEST` sleeps by condition, not wall clock | ~4 s | medium |
| 7 | Phase 2 headless batching | ~3–4 s | medium |
| 8 | Phase 3 in-process GL harness | ~25 s | **high** |

Items 2–5 are a single afternoon and take the common case from 135 s to ~8 s.

## 3. The tiers

Runtimes below are measured sums of the current per-test times, *before* any of
§2 lands. They only get better.

### Tier 0 — build only (~63 s)
No fixtures. What you run while iterating on a change that is not finished.

### Tier 1 — pre-commit smoke (**~7.7 s**, 11 checks)
`driver.sh --fast`. Broadest coverage per second: graph integrity,
serialization round trip, pin identity, undo, teardown, and the three
near-free pre-`glfwInit` invariants.

```
UNDOTEST:10  PATCHTEST:30  ROUNDTRIPTEST:35  PINDUPTEST:10  BYPASSTEST:30
DELETECRASHTEST:8  DSPTEST:1  PERFMATRIXTEST:1  AUDIOPDCTEST:1
RECSYNCTEST:1  AUTOSAVEMARKERTEST:1
```

`ROUNDTRIPTEST` + `PINDUPTEST` between them spawn every registered node type,
so a new node that fails to construct, save, or lay out its pins is caught here
for 3.2 s. This is the tier that runs on every commit.

### Tier 2 — subsystem sweeps (run only the group(s) your diff touches)

| Group | Trigger paths | Checks | Runtime |
|---|---|---|---|
| **audio** | `src/audio/**`, `src/nodes/Audio*`, `*Sequencer*`, `*Resonator*` | `AUDIOPARAMSWEEPTEST AUDIOTEARDOWNSWEEPTEST AUDIOGRAPHTEST AUDIOLIFECYCLETEST AUDIORECOVERYTEST AUDIOPDCTEST RECSYNCTEST RECEXPORTTEST` | 11.6 s |
| **3d/geometry** | `src/nodes/Geometry*`, `*Mesh*`, `src/core/Mesh.*`, `*3D*`, `*Ocean*`, `*Path*` | `GEOTEST MESHOPTEST TEXT3DTEST PATHOCEANTEST SHADOWTEST MATFRAMETEST MAPTEST PADPATHTEST 3DTEST TRANSFORMSWEEPTEST MAPPINGSWEEPTEST REVISIONSWEEPTEST ENVTEST WRAPTEST` | 14.1 s |
| **ui/editor** | `src/main.cpp` node-body & canvas regions, `src/core/NodeViewport.*` | `GROUPTEST COMMENTTEST HIDETEST SELECTTEST DISTRIBUTETEST INSTANCESELECTTEST MINIVIEWPORTTEST COLORTEST PALETTETEST DRAGTEST WTDRAGTEST` | ~11 s |
| **modulation** | `src/core/Modulation.*`, macro/perf-matrix code | `MACROTEST MODBOUNDSTEST MODMATRIXTEST PERFMATRIXTEST` | 3.3 s |
| **video/export** | `OutputNode`, `src/platform/**` recorder/muxer/decoder | `VIDEOAUDIOTEST VIDEOSPEEDTEST RECEXPORTTEST RECSYNCTEST` | 69 s → ~8 s after fix #1 |
| **media/browser** | scanner code, browser panel | `SAMPLERDRAGTEST MEDIADRAGTEST PLUGINDRAGTEST PLUGINSCANTEST BROWSERSORTTEST` | ~4 s |
| **compositing/misc** | `src/nodes/Blend*`, `Curves*`, `Feedback*`, filters | `PHASEATEST PHASECTEST PHASEDTEST PHASEETEST PHASEFTEST PHASE1TEST PHASE4TEST BUGTEST FIXTEST LIVETEST REMOVEBGTEST` | ~11 s |

A diff touching `src/core/Patch.cpp`, `NodeFactory.*`, or `INode.h` is
cross-cutting — those escalate straight to Tier 3.

### Tier 3 — full gate (135 s now, ~50 s after §2 items 1+3)
Everything, plus the screenshot smoke. Pre-push, pre-release, and after any
cross-cutting change. Not per-commit.

## 4. Making the tier automatic

The tier should be derived from `git diff --name-only`, not chosen by hand —
a tier you have to remember to raise is a tier you forget to raise.

```
driver.sh --fast              # Tier 1 explicitly
driver.sh --auto              # Tier 1 + the Tier 2 groups the diff touches
driver.sh --group audio,3d    # Tier 2 explicitly
driver.sh --full              # Tier 3 (current default behaviour)
```

`--auto` is the one to wire into the commit habit. Keep `--full` as the default
when no flag is passed, so an unthinking invocation is still the safe one, and
have `--auto` print which groups it selected and which it skipped — a selector
that silently skips a group reads as "covered everything" when it wasn't.

## 5. Guardrails (kept from the original plan, narrowed)

1. **Verdict parity** on Tier 3 before/after each §2 change: every fixture's
   pass/fail/xfail must match a baseline commit exactly.
2. **VSync-off determinism**: several fixtures assert on frame *indices*
   (`frameId == 62`), not elapsed time, so uncapping the frame rate is safe by
   construction — but re-run the timing-sensitive ones (`PHASE1TEST`, already
   flaky on particle timing; `VIDEOSPEEDTEST`; `AUDIORECOVERYTEST`) 3× each to
   confirm no new flake.
3. **Never shorten a frame budget to save time.** Budgets are ceilings; every
   fixture already exits on its own verdict. Cutting one only risks the
   "no verdict printed" failure mode.
