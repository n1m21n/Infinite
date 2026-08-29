---
name: pillar-parity-audit
description: Audit Infinite pillar by pillar — nodes, shortcuts, settings, rendering, panels, modulation, audio/video/geometry, I/O, performance, save/recovery, plus the platform-contract, dual-path-numerics, typography and distribution pillars — and report for each one what evidence exists that it works, on WHICH platform (macOS vs Windows), produced by a machine rather than by a claim. Use when asked "does X work on both versions", "is everything still working", "what's actually verified on Windows", "audit the whole app", "what are we not testing", or before a release when the question is coverage rather than a single failing area. Not a replacement for run-infinite-hygiene (that's the pre-commit gate); this is the map of what that gate does and does not prove.
---

Paths are relative to the repo root (`/Users/namansoni/infinite`).

## Run it

```bash
.claude/skills/pillar-parity-audit/audit.sh --matrix        # the coverage map, instant
.claude/skills/pillar-parity-audit/audit.sh --static-only   # source-tree checks, no build, ~1s
.claude/skills/pillar-parity-audit/audit.sh --new-only      # everything the hygiene gate skips
.claude/skills/pillar-parity-audit/audit.sh                 # full audit, both lanes
.claude/skills/pillar-parity-audit/audit.sh --pillar P4     # one pillar
```

Needs an existing `build/` (`cmake --build build -j8`) for the dynamic lane.
`--static-only` needs nothing.

---

## Why this exists

`run-infinite-hygiene` answers *"did I break something?"*. It cannot answer
*"is this pillar of the app actually verified, and on which OS?"* — because it
is organised by fixture, runs on macOS only, and its passing output looks
identical whether a pillar has twelve assertions or none.

Three specific blind spots motivated this skill, all confirmed against the tree:

1. **Windows has machine evidence for five checks, total.**
   `.github/scripts/headless-tests.sh` runs `DSPTEST`, `PERFMATRIXTEST`,
   `AUDIOPDCTEST`, `REMOVEBGTEST` and a crash-only `AUDIOPARAMSWEEPTEST`.
   Everything else on Windows rests on code review plus a human Parallels pass.
   The hygiene suite's ~54 GL fixtures run on **neither** Windows CI nor
   Windows hardware.
2. **13 fixtures with unambiguous verdicts are not in any gate.** Verified
   passing here, excluded from `driver.sh`: `INSTANCESWEEPTEST`,
   `RENDER3DLIVETEST`, `RENDER3DCACHESWEEPTEST`, `BROWSERSORTTEST`,
   `TRANSPORTCLOCKTEST`, `NOTEFANOUTTEST`, `UTILTEST`, `CYCLESHAPERTEST`,
   `PARTICLETEST`, `AUDIOPCMTEST`, `DSPTEST`, `RECTEARDOWNTEST`, `FPSTEST`.
   `audit.sh --new-only` runs exactly these.
3. **Three pillars have no executable evidence anywhere** — shortcuts,
   settings persistence, and glyph metrics. See the backlog below.

The compiling-is-not-evidence lesson is already written into this repo's
history: `PortableFft::Inverse` returned time-reversed audio for months while
the macOS branch sounded perfect and `INFINITE_DSPTEST` printed
`PAULSTRETCHTEST OK` **on the real Windows runner**.

---

## The pillars

`pillars.tsv` is the machine-readable manifest; this is what each one means and
where its evidence lives.

### The ten you'd name first

| # | Pillar | What "working" means | Evidence |
|---|---|---|---|
| **P1** | Nodes | Every registered type constructs, draws, survives copy/paste + save/load, has no duplicate pin ids, and can be deleted mid-graph without a dangling pointer | `ROUNDTRIPTEST`, `PINDUPTEST`, `DELETECRASHTEST`, `INSTANCESWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST` |
| **P2** | Shortcuts | Every chord in `DrawShortcutsWindow`'s `kShortcuts[]` (`src/main.cpp:22675`) fires its action, and `MODKEY` maps to Ctrl on Windows as well as Cmd on macOS | **static only** — see backlog |
| **P3** | Settings & state | Theme, recents, category colours, sample/media/plugin indexes and `imgui.ini` all resolve through `AppPaths::AppSupportDir()`, survive a restart, and land in the right per-user location on each OS | **static only** — see backlog |
| **P4** | Rendering | 3D geometry, shadows, materials, maps, environment lighting and the Render 3D caches all produce correct pixels and keep advancing | `3DTEST`, `SHADOWTEST`, `MATFRAMETEST`, `MAPTEST`, `ENVTEST`, `RENDER3DLIVETEST`, `RENDER3DCACHESWEEPTEST` + the hygiene screenshot |
| **P5** | Panels | Browsers sort and filter, the modulation matrix and performance matrix dock and round-trip | `BROWSERSORTTEST`, `MODMATRIXTEST`, `PERFMATRIXTEST` |
| **P6** | Modulation | A cable reaches every param that registers a `ParamRef`; ranges clamp; macros and note fan-out route correctly | `MODMATRIXTEST`, `MODBOUNDSTEST`, `MACROTEST`, `NOTEFANOUTTEST`, and the `node-param-audit` skill for the inventory |
| **P7** | Audio / video / compositing / geometry | The four content engines each do their basic job end to end | `AUDIOGRAPHTEST`, `AUDIOPCMTEST`, `DSPTEST`, `CYCLESHAPERTEST`, `VIDEOAUDIOTEST`, `GEOTEST`, `MESHOPTEST`, `PARTICLETEST`, `UTILTEST` |
| **P8** | Input / output | Device enumeration, file dialogs, drag-and-drop of samples/media/plugins, recording teardown, OSC, Spout/Syphon | `SAMPLERDRAGTEST`, `MEDIADRAGTEST`, `PLUGINDRAGTEST`, `RECTEARDOWNTEST` |
| **P9** | Performance | The frame limiter hits its budget and the transport clock advances and freezes on cue | `FPSTEST`, `TRANSPORTCLOCKTEST` (`EDPERFTEST` and `CACHETEST` print raw numbers only — read the log, they have no verdict) |
| **P10** | Save / recovery / crash | Patches round-trip, autosave writes and its marker drives the recovery prompt, and a crash leaves evidence behind | `PATCHTEST`, `AUTOSAVETEST`, `AUTOSAVEMARKERTEST`; crash diagnostics are static-checked only |

### The five that aren't obvious, and are where the real bugs came from

These are the additions — pillars that don't describe a *feature* but a
*property the whole app depends on*, each drawn from a defect that actually
shipped here.

| # | Pillar | The failure mode it guards |
|---|---|---|
| **P11** | **Platform-layer contract** | A `Platform::` function declared in `Platform.h` and implemented only in `Platform.mm` builds clean on macOS and is an unresolved external symbol on Windows CI. A new `src/platform/win/*.cpp` missing from `WIN32_SOURCES` fails the whole link. A `_WIN32` conditional in `src/nodes/` is a code path nobody with commit access can execute. All three are checked by `static-checks.sh` — 131 declarations, both sides, in about a second. |
| **P12** | **Distribution integrity** | The shipped exe imported `MSVCP140.dll` and died before `main()` on any clean Windows install, while every test passed on a runner that had Visual Studio installed (defect 1.11). Build success is not shippability. `build.yml` now scans the import table; this pillar is the reminder that the property is separate from the build. |
| **P13** | **Dual-path numeric equivalence** | Every `#if defined(__APPLE__) / #else` numeric split is two implementations of which **only one is ever executed by anyone on this project**. `PortableFft::Inverse` is the canonical case. The rule: an assertion that compares the two branches on the same input, not one that exercises whichever branch your machine compiled. `static-checks.sh` enumerates the split files — currently **five**, not the four `windows-parity`'s SKILL.md lists: it omits `src/audio/dsp/MolderDsp.cpp`. |
| **P14** | **Text and typography** | Cap height measured from the wrong glyph makes *all text in the app* render at the wrong size (defect 1.4, open). CoreText vs GDI+ `GraphicsPath` is a dual-path split whose output is geometric, not numeric, so P13's DSP-style assertion doesn't reach it. Nothing tests it on either platform today. |
| **P15** | **UI/UX conformance** | The per-node mini viewport, colour picker and palette behave as the design grammar says (`audio-node-ui` skill). A node that renders but violates the grammar passes every other pillar. |

---

## Reading the grades

`audit.sh --matrix` prints a grade per pillar. They mean exactly this:

- **A** — executed by machine on both platforms. Only **P7** qualifies, and
  only because `DSPTEST` / `AUDIOPDCTEST` / `REMOVEBGTEST` happen to be
  headless enough for the Windows runner.
- **B** — executed on macOS, with the Windows half asserted statically
  (P11, P12, P13).
- **C** — executed on macOS only. Windows rests on review plus a human
  Parallels pass (`docs/WINDOWS_VERIFICATION.md` Part 2).
- **D** — no execution anywhere (P2, P3, P14).

**Do not report a grade-C pillar as "working on both versions."** It is
working on macOS and unfalsified on Windows. That distinction is the entire
point of this skill.

---

## Backlog — the three fixtures that would close the D grades

Each follows the existing pattern in `src/main.cpp`: an `INFINITE_<NAME>TEST`
env-var gate, a fixture graph, a `printf` verdict ending in `OK` or containing
`FAIL`. Add the name to `pillars.tsv` and to `run-infinite-hygiene/driver.sh`'s
`TESTS` array once it lands; add it to `.github/scripts/headless-tests.sh` too
if it returns before `glfwInit()`, which is what upgrades its pillar from C to A.

**1. `INFINITE_SHORTCUTSWEEPTEST` (P2 → C, and A if headless).**
Iterate `kShortcuts[]` itself rather than a hand-written list, so a shortcut
added to the table without a handler fails immediately. For each entry, parse
the key string, inject the chord through `io.AddKeyEvent` (the pattern already
exists at `src/main.cpp:35501`, and `EDPERFTEST` shows the
`AddFocusEvent(true)` + `glfwFocusWindow` dance a headless run needs), and
assert the observable effect — undo depth moved, node count changed,
`gShortcutsOpen` flipped. Run each chord twice: once with `KeySuper`, once with
`KeyCtrl`. The handler at `src/main.cpp:44314` uses
`io.KeyCtrl || io.KeySuper`, so both must fire; that equivalence is the
Windows half of this pillar and it is testable from macOS.

**2. `INFINITE_SETTINGSTEST` (P3 → C).**
Round-trip everything mutable through `AppPaths::AppSupportDir()`: write a
category-colour override, a recents entry and a scanner index; re-read them
through the same loaders (`CategoryColors.cpp:211`, `Patch.cpp:25`,
`PluginScanner.cpp:21`); assert the values survive. Then assert the resolved
directory matches the platform's expectation and that `EnsureDir` actually
created it — `AppPaths.h`'s `EnsureDir` ignores the `mkdir` result and always
returns `true`, so nothing currently notices a failure (defect 1.10 item 5).
This one is headless and would run on the Windows runner, which is where
defect 1.5 lives.

**3. `INFINITE_GLYPHMETRICSTEST` (P14 → C, A if headless).**
Take `Platform::TextOutline`'s output for a known string at a known size and
assert the measured cap height, advance width and bounding box against fixed
tolerances. This is the assertion whose absence let defect 1.4 through: the
Windows path measures cap height from the wrong glyph and every string in the
app renders at the wrong size, invisibly to the build and to every fixture.
Numbers baked from the macOS path become the contract the GDI+ path must meet.

---

## What this skill does not do

- It does not run anything on Windows. Nobody on this project can. The Windows
  column is CI's five headless checks, `static-checks.sh`, and
  `docs/WINDOWS_VERIFICATION.md` Part 2's manual protocol — nothing else.
- It does not replace `run-infinite-hygiene`. That stays the pre-commit gate.
  This is the coverage map, run before a release or when the question is
  "what aren't we testing?"
- It does not judge whether a node is *good* (`infinite-code-review`), whether
  a param is modulatable (`node-param-audit`), or whether new code will port
  (`windows-parity`). It reports what is verified and by what.
