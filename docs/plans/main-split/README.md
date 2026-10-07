# Split `src/main.cpp` — execution brief

Goal: break the 107,480-line `src/main.cpp` into focused TUs so that (1) editing
one panel / node body / fixture recompiles a few thousand lines, not 107k,
(2) a full build parallelises across cores, and (3) the file layout matches
how the app is actually organised. **Zero behaviour change.** Every step is a
move, not a rewrite.

Skills to load before starting: `codebase-navigation`, `git-branch-workflow`,
`run-infinite-hygiene`, `windows-parity`, `linux-parity`, `semi-brain`
(query it once on "split main.cpp into TUs: shared-state header vs per-TU
extern" before Step A1).

---

## 1. What is in the file today (measured 2026-10-07, at `cf64c025`)

```
main.cpp  107,480 lines
├─ 1–298        includes (232 #include, 87 of them nodes/*)
├─ 299–52037    namespace { … }            51.7k  ← UI + editor state + graph ops
│   ├─ ~1,027 g* globals (gNodes @740, gEditor, gDroppedFiles, …)
│   ├─ widgets/theme   ModCheckbox 4222 (1.6k), ApplyTheme 6598, knob rows
│   ├─ node bodies     Draw*Body / Draw*Params / visualizers (~9k–28k)
│   ├─ panels          Arrange 33996–35223–45032 (~7k), ModMatrix 30935,
│   │                  Perf 40080/41805, Library/Plugin search 18969/19457,
│   │                  Settings 47703, Help/Shortcuts 42431–43041, Minimap 50983
│   └─ graph ops       RegisterNodes 6312, Connect* 7799, RebuildAudioTopology
│                      43919, Field hosts 46313/46527, BuildPatchData 46793,
│                      ApplyPatchData 48899, Undo/Redo ~50310
├─ 52038–74537  self-test fixtures + headless  22.5k  ← pure test code
│   ├─ DSP/audio fixtures  52132–61189 (DspTest, Reverb, Sampler, DrumSeq 1.2k…)
│   ├─ Field tests         61189–66763 (FieldSampleTest alone 1.6k)
│   ├─ rec/AV/PDC/MIDI     66764–68982
│   ├─ plugin/VST3/browser/net/misc 68983–71884
│   ├─ ApplyModulationAndPalette 71203 (runtime, NOT a test — misplaced)
│   └─ headless jobs, bench scenes, ParamKeyJoiner, HeadlessTick 72059–74537
└─ 74538–107480 int main()                     32.9k  ← one function
    ├─ startup                    74538–78113   3.6k
    └─ while (!glfwWindowShouldClose) 78114–    29k
        ├─ menu bar 79216, layout/docking, drop handling 80487
        ├─ 108 in-frame `if (getenv("INFINITE_*TEST") && frameId==N)` blocks
        │   = ~15,000 lines, evaluated (getenv) EVERY frame
        ├─ ed::Begin 80448 … node loop 99129 (BeginNode 99287, type dispatch 99417–100263)
        ├─ links 100393, splice 100558, new connections 100636
        ├─ keyboard 100940–102000, deletions, drag undo, snap
        ├─ popups 102082 (modbind 102454), node browser 104192
        ├─ floating windows 104516, update modal 105099
        └─ modulation + cook 105259
```

Already-known cost: MSVC x64 ICEs on this TU at /O2, so `CMakeLists.txt:748`
forces `/Od` on the whole file on Windows — the shipped Windows UI is
unoptimised. That comment ends "the real fix is for this file to stop being
37k lines". This brief is that fix.

## 2. The one real obstacle: anonymous-namespace globals

Everything in 299–52037 has internal linkage. Any function moved out still
needs `gNodes`, `gEditor`, theme constants, etc. Decision:

| Option | Verdict |
|---|---|
| `inline` variables in a header | **No** — every TU then sees every type; header edits rebuild everything; init order becomes per-TU |
| **`namespace app {}` + `extern` declarations in topic headers, definitions in one `src/app/AppState.cpp` in original order** | **Yes** — one definition site keeps static-init order identical; headers forward-declare node types so they stay light |
| Wrap globals in a `struct AppState` singleton | Later, maybe — rewrites ~every line, violates "move not rewrite" |

Rules for the move:
- Rename `namespace {` → `namespace app {`. Names in other TUs can't collide with it, and nothing changes for code still inside `main.cpp`.
- Helpers used by exactly one destination TU stay `static` there. Helpers used by 2+ TUs get a declaration in the topic header.
- Default arguments live **only** on the header declaration (e.g. `ApplyPatchData(…, std::map<int,int>* outRemap = nullptr, …)`).
- `const float kX = …` at namespace scope → `inline constexpr float kX` in `app/Layout.h` (that's safe — constants have no init-order issue).
- Function-local `static`s keep their meaning as long as a block moves into exactly one function. Never duplicate a block into two call sites.
- Headers include **forward declarations**, not `nodes/*.h`. The node headers go only in the `.cpp` files that dereference them. Incremental speed comes from this, so check it on every step.

## 3. Target layout

```
src/app/
  AppState.h / .cpp        extern decls + the ONE definition site of all g* globals
  Layout.h                 kPreviewSize, kParamWidth, … (inline constexpr)
  Main.cpp                 int main(): startup + frame loop that only calls functions
  Startup.cpp              74538–78113 body → InitApp(argc, argv, AppBoot&)
  frame/
    FrameContext.h         struct FrameCtx { frameId, window, graphW/H, … main() locals the loop shares }
    MenuBar.cpp  Docking.cpp  DropHandling.cpp  Keyboard.cpp
    Canvas.cpp             ed::Begin … ed::End, node loop, links, connections
    Popups.cpp  FloatingWindows.cpp  CookFrame.cpp (+ ApplyModulationAndPalette)
  ui/
    Theme.cpp  Widgets.cpp (ModCheckbox, ModSlider, knob rows, AudioKnobRow, ADSRLayout)
  bodies/                  Draw*Body / Draw*Params, by family
    AudioBodies.cpp  AudioVisualizers.cpp  SequencerBodies.cpp
    GeometryBodies.cpp  VisualBodies.cpp  ModulatorBodies.cpp  FieldBodies.cpp
  panels/
    ArrangePanel.cpp  ArrangeClipSettings.cpp  ArrangeCompose.cpp
    ModMatrix.cpp  PerfPanel.cpp  LibrarySearch.cpp  PluginSearch.cpp
    Settings.cpp  HelpWindows.cpp (Help, Shortcuts, SpecificNodeHelpText)
    ViewportPanel.cpp  Minimap.cpp
  graph/
    NodeRegistry.cpp (RegisterNodes, InputCountFor)  Connections.cpp
    AudioTopology.cpp  PatchIO.cpp (Build/ApplyPatchData, ArrangeModelToPatchData)
    UndoRedo.cpp  FieldGraphHosts.cpp
  selftest/                (compiled into the app, same as today)
    FrameTests.h           registry: { "INFINITE_FOOTEST", frame, fn }
    FrameTests_*.cpp       the 108 in-loop blocks, one function each
    DspFixtures*.cpp  FieldTests*.cpp  AvRecTests.cpp  PluginTests.cpp
    MiscTests.cpp  Headless.cpp (HeadlessTick, ExplainLive, ParamKeyJoiner)
    BenchScenes.cpp
```

Size target: no `.cpp` over ~5k lines, except `ArrangePanel.cpp`
(`DrawArrangePanelContent` alone is 4.5k — split it inside only if it's simple).

## 4. Execution — 3 blocks, one branch each, commit per step

Each block: `feature/main-split-<a|b|c>` off main, merge back when its gate
passes. **Every step = build passes on mac + `cmake -S . -B` configure still
works + the step's tests pass + one commit.** Never batch two steps into one
commit; a bad move must be revertable on its own.

### Block A — test code out (lowest risk, ~37k lines, biggest single win)

| Step | Move | Notes |
|---|---|---|
| A0 | Baseline: `touch src/main.cpp && time cmake --build build -j8` ×3, record median; and `-ftime-trace` on main.cpp once to see header vs codegen share | write numbers into §6 |
| A1 | `namespace {` → `namespace app {`; add `src/app/AppState.h` with extern decls **only for globals Block A needs** (grep each fixture for `\bg[A-Z]\w*`) | definitions stay in main.cpp for now |
| A2 | 52038–74537 fixtures → `src/app/selftest/*.cpp` (grouping per §3) | pull `ApplyModulationAndPalette` (71203) and the per-clip mod-bypass helpers (71071) into `frame/CookFrame.cpp`: they're runtime code |
| A3 | 108 in-loop `getenv(...) && frameId==N` blocks → `void FrameTest_Foo(FrameCtx&)` + registry table. At startup, read the env once and fill a small `std::vector` of active tests; the loop calls `RunActiveFrameTests(ctx)` | removes ~108 `getenv` calls per frame (perf win). The block keeps its exact frame number and order. Any block that writes to a main() local → that local goes into `FrameCtx` |
| A4 | Update `CMakeLists.txt` `COMMON_SOURCES` (glob is fine for `src/app/**` but explicit lists match the repo — use explicit) | Linux + Windows lists too |

Gate A: full `run-infinite-hygiene/driver.sh` + `audio-node-sweep` + every
`INFINITE_*TEST` whose block moved (the registry makes it easy to list them). Then
`git diff --stat` must show only moves: compare
`git show main:src/main.cpp | wc -l` against `wc -l` of main.cpp + new files.
They should match within a few hundred lines of headers/signatures.

### Block B — the namespace (51.7k) by domain

Order = least coupled first: **Layout.h constants → ui/Theme+Widgets →
bodies/* → panels/* → graph/* → AppState.cpp (definitions move last)**.

- One subfolder per step (e.g. B3 = all of `bodies/`, ~7 files).
- Before each move, `grep -n` every function name in the chunk across the rest of main.cpp: those are the declarations its header needs.
- `AppState.cpp` definitions move last, **in their original textual order**, so static init order stays the same.

Gate B: hygiene suite + `node-ui-sweep` + `panels-sweep` + `modulation-sweep` +
ROUNDTRIPTEST + `cable-logic-sweep` (after its check.py is repointed, see §5).

### Block C — `int main()` (32.9k) into frame stages

1. C1 `Startup.cpp`: 74538–78113 → `InitApp`. Locals that survive into the loop → `AppBoot` struct returned.
2. C2 Define `FrameCtx` from the locals the loop really shares (find them by compiling each extracted function — the compiler lists them).
3. C3 Extract the stages top to bottom (MenuBar, Docking, DropHandling, Canvas, Keyboard, Popups, FloatingWindows, CookFrame). Each one is `void DrawX(FrameCtx&)`, called from the loop in the same order.
4. C4 `Main.cpp` is now ~300 lines. **Remove the MSVC `/Od` override** (`CMakeLists.txt:722–749`) and confirm the Windows CI build passes at /O2. If one file still ICEs, scope `/Od` to that file only and keep the comment.

Gate C: everything in A + B, plus `output-projection-sweep`, `av-sync-sweep`,
`shortcuts-sweep`, Windows + Linux CI green, and the user's manual app
launch.

## 5. Things the split breaks unless you fix them in the same step

| What | Where | Fix |
|---|---|---|
| Static checkers that parse `main.cpp` | `.claude/skills/cable-logic-sweep/check.py`, `shortcuts-sweep/check.py`, `pillar-parity-audit/static-checks.sh`, `tools/manual/cards.py`, `tools/linux/*.sh` | make them read `src/main.cpp` + `src/app/**/*.cpp`. A checker that silently finds 0 matches means it's broken, so each one must fail loudly if a file is missing |
| `main.cpp:NNNN` citations | 51 files in `.claude/skills` + `docs` | update `codebase-navigation` hotspot map to `file:function` form (no line numbers) at the end of each block. Leave historical docs alone |
| semi-brain extractors | `tools/semi-brain/1_extractors/build_main_cpp_regions.py`, `l4/regions.py` | teach it the new paths; commit brain files separately (owner rule) |
| Help-text coverage | the 4 hand tables + per-OS `#if` (memory: node help coverage) | move as one unit into `HelpWindows.cpp`; don't split the `#if` branches |
| `#ifndef NDEBUG` debug tools (UI Debugger/Style Editor, lines 2249, 6283, 6314) | | keep the guards around the moved code, and check that a Release build still leaves them out |
| Platform `#if` blocks (42572–43405, 48097–48205, 51137–51861) | | move with their function; no new `_WIN32` in node code (`windows-parity`) |

## 6. Build-speed levers (do after Block A, measure each, keep only if faster)

| Lever | How | Expected |
|---|---|---|
| Ninja instead of Makefiles | `build/` is a Makefile tree; `cmake -G Ninja -B build-ninja` | faster no-op + better scheduling |
| ccache locally | `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` (CI already uses it) | branch switches / rebuilds |
| PCH for the new app TUs | `target_precompile_headers(Infinite PRIVATE <vector> <string> <map> imgui.h imgui_node_editor.h)` — NOT node headers | cuts the cost of each new TU parsing imgui/std again |
| Debug-ish dev build | `build/` is `-O3` Release; an `-O1 -g` dev tree for iteration | big on the TUs you're editing |

Record each as: lever → median of 3 → keep/drop (keep only if it's faster, as in
`run-infinite-hygiene` "Efficient routes").

| Measure | Before (A0) | After A | After B | After C |
|---|---|---|---|---|
| `touch` one panel file → rebuild | n/a (=full main.cpp) | | | |
| `touch src/main.cpp` → rebuild | 182.3 s (median of 3) | 74.4 s (1 run) | | |
| clean full build `-j8` | | | | |
| largest TU (lines) | 107,480 | 69,142 (main.cpp); largest new TU ~3,000 | | |

## 7. Stop conditions

- A move needs a logic change to compile → stop. Note it here and leave that piece in main.cpp for now.
- The hygiene suite shows a new failure that isn't a stale checker → revert that one step's commit and go back to the last green step.
- Static-init-order symptom (crash before `main`, empty registries) → the AppState definition order changed. Put it back to the original order.

## 8. Prompt to paste into a fresh session

> Execute `docs/plans/main-split/README.md`, Block A only. Load the skills
> listed at the top first. Branch `feature/main-split-a` off main. Do A0
> (record the baseline in §6), then A1–A4, one commit per step, build after
> every step. Moves only: no renames beyond `namespace {`→`namespace app {`,
> no logic edits, keep each function body byte-identical. Run Gate A,
> fill §6's "After A" column, then repoint the §5 checkers that Block A
> broke. Report: line counts per new file, rebuild timings before and after,
> and any piece you left in main.cpp per §7, with the reason.

Run each block in its own session, only after the previous block is merged to main.

**Block B**

> Execute `docs/plans/main-split/README.md`, Block B only (Block A is merged).
> Load the skills listed at the top first. Branch `feature/main-split-b` off
> main. Move the `namespace app {}` contents out of main.cpp in this order,
> one commit per step: Layout.h constants → ui/Theme+Widgets → bodies/* →
> panels/* → graph/* → AppState.cpp (definitions last, in original textual
> order). Before each move, grep each function name in the chunk across the
> rest of the codebase to build its header. Headers forward-declare node types and
> never include `nodes/*.h`. Moves only, bodies byte-identical. Build after
> every step. Run Gate B, repoint the §5 checkers and update the
> `codebase-navigation` hotspot map to file:function form. Fill §6
> "After B". Report: line counts per new file, rebuild timings (touch one
> panel file, touch main.cpp, clean build), and anything left behind per §7.

**Block C**

> Execute `docs/plans/main-split/README.md`, Block C only (Blocks A and B
> are merged). Load the skills listed at the top first. Branch
> `feature/main-split-c` off main. C1: startup → `Startup.cpp` (`InitApp`
> returning `AppBoot`). C2: define `FrameCtx` from the main() locals the
> loop actually shares. C3: extract the frame stages top to bottom into
> `src/app/frame/*.cpp` (MenuBar, Docking, DropHandling, Canvas, Keyboard,
> Popups, FloatingWindows, CookFrame), each called from the loop in the same
> order. One commit per stage. C4: remove the MSVC `/Od` override and get
> Windows CI green at /O2. If one file still ICEs, scope /Od to that file only.
> Moves only, bodies byte-identical. Run Gate C including Windows + Linux
> CI. Fill §6 "After C". Report: final main.cpp size, the largest TU,
> timings for all three blocks, and anything left behind per §7.


## Block A result (2026-10-07, `feature/main-split-a`)

- A1 `81a6bb42` namespace rename + `AppCommon.h`; A2 `97b8c873` fixtures to `src/app/selftest/*.cpp`; A3 `42f32d83` in-loop test blocks to `src/app/frame/FrameTests1-6.cpp`; A4 CMake cleanup.
- Generator: moves are produced mechanically from the pristine baseline (exposure analysis -> header declarations, compiler-driven for A3); nothing hand-edited.
- A3 deviation from the brief: no registry. Each moved block is `if (getenv(...)) {...}` verbatim inside `FrameTest_<NAME>(int frameId, GLFWwindow* window)`, called unconditionally at the block's original position in the loop (order, ImGui context and `ed::` scope unchanged). 136 of 156 blocks moved; 20 stay in `main()` (section 7): they use main/loop locals (`searchBuf`, `offlineClockDone`, the `sUnpackTest*`/`sPhase1*` loop statics, `ShapeResFixtureReport`), `return` from the loop, have a block-scope `extern`, or hit a name collision (`lastFrameMs`).
- Gate A: hygiene `--full` 110/112 on first run; the 2 misses are load-sensitive flakes that pass in isolation: PLUGINDRAGTEST (also fails 1/12 on a `main` build), RECEXPORTTEST (passed alone).
