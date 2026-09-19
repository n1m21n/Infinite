# Prediction step 1: the movement log

You are building **step 1 of the Prediction modulator** in Infinite (`/Users/namansoni/infinte`,
C++17, ImGui + OpenGL). The design is [README.md](README.md). Read §2.4, §2.5, §3.1 and §3.2 first.
This step writes **no model and no node**. It only records param movement to disk, correctly and cheaply.

Line numbers are from commit `35221c1`. Where a number has drifted, **re-grep the symbol**: the symbol
is authoritative, the number is a convenience.

## Start

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-01-movement-log
```

Skills to load: `codebase-navigation`, `windows-parity`, `linux-parity` (the file and thread code is
cross-platform), `invariant-interaction-audit` (the logger must not change any param value).

## Files to read first

| File / symbol | Why |
|---|---|
| `src/main.cpp:61362` `ApplyModulationAndPalette` (ends ~61675) | the one place where every writer has landed; the hook goes at its end |
| `src/main.cpp:61368` perf-matrix deferred writes (`gPerfPendingWrites`) | writer site 1: tag `perf` |
| `src/main.cpp:61460` `MacroNumBoxNode` / `MacroTriggerNode` branches, `61488` generic modulator write | writer site 2: tag `modulator` |
| `src/main.cpp` expression branch right after `61490` (`modulation.ExpressionFor`) | writer site 3: tag `expression` |
| `src/main.cpp:61578–61594` gesture playback loop (`GetPlaybackValue`) | writer site 4: tag `gesture` |
| `src/core/Modulation.h` `ParamRef`, `FrameParams()`, `KnownParam()` | what is registered each frame; `valueToPos` for fader position |
| `src/core/GraphNode.h:50` `uid`, `typeName` | stable key + node type for the `KEY` record |
| `src/main.cpp:1465` `gNextNodeUid`, `7447` uid minting, `43469` uid restore in `ApplyPatchData` | uid lifetime |
| `src/main.cpp:43220` `ApplyPatchData`, `42066` `NewPatch`, undo/redo `44216` / `44245` | where values jump without a hand: emit `MARK`, re-baseline |
| `src/main.cpp:64664` (offline render) and `86848` (normal frame): the two `ApplyModulationAndPalette(frameId)` calls | log only from the normal frame |
| `src/core/Transport.h` `IsPlaying()`, `Tempo()`, `Beats()`, `BeatsPerBar()` | `TRANSPORT` records |
| `src/platform/AppPaths.h` `AppSupportDir()` | log folder (macOS/Windows/Linux branches already exist) |
| commit `436bd62` (recorder worker queue, `src/platform/Platform.mm`, `src/platform/win/MediaWin.cpp`) | precedent: bounded queue, drop under backpressure, drain + join on stop |

## What to build

### 1.1 `src/core/MovementLog.h/.cpp` (new pair, add to `CMakeLists.txt` next to `src/core/Modulation.cpp`)

```cpp
namespace MovementLog {
   enum class Source : uint8_t { Hand, Perf, Modulator, Expression, Gesture, Prediction, Other };
   enum Flags : uint8_t { kCorrection = 1 };
   enum class Mark : uint8_t { SessionStart, SessionEnd, PatchLoaded, PatchNew, Undo, Redo };

   void Start();                       // app start: open session file, spawn writer thread
   void Stop();                        // app quit: drain, flush, join
   void SetEnabled(bool on);           // Settings checkbox (default on)
   void NoteWriter(int nodeIndex, int paramIndex, Source s);   // called at the 4 writer sites
   void NoteMark(Mark m);              // undo/redo/load/new: next Capture re-baselines silently
   void Capture(double t);             // end of ApplyModulationAndPalette, normal frames only
}
```

### 1.2 Capture (main thread, the hot path)

At the end of `ApplyModulationAndPalette`, **only** from the normal-frame call (`86848`). Not from the
offline render (`64664`).

**Test isolation, decided once in `Start()`** (scan `environ` once; never call `getenv` per frame):
if **any** `INFINITE_*` variable is set (the self-tests, `INFINITE_AUDIOUITEST`'s visual fixture,
`INFINITE_SCREENSHOT_FRAME` documentation shots), logging **and** `stats.bin` persistence (step 2) are
off, unless `INFINITE_MOVELOG_DIR` is also set. In that case both go to that directory, never the
user's folder. Every prediction test (steps 1–6) sets `INFINITE_MOVELOG_DIR` to a temp dir.
A lock file in the log folder stops a second running instance from writing the same files.

For each `ref` in `Modulation::FrameParams()` with `ref.value != nullptr`:

1. `key = (uid, ref.paramIndex)`. Map `nodeIndex → uid` through a small cache rebuilt only when
   `gNodes` changes. `FindNodeByIndex` per param per frame is too slow.
2. `pos = ref.valueToPos ? ref.valueToPos(*ref.value, min, max) : (*ref.value - min) / (max - min)`;
   `q = round(clamp(pos,0,1) * 65535)`.
3. If this is the first sighting in this file, emit `KEY` (uid, paramIndex, `typeName`, `ref.name`,
   min, max, isEnum/isBool, has-curve).
4. **Mark epochs, not "this frame".** `NoteMark` bumps a global `epoch`. Each key stores the epoch of
   its last baseline; if `key.epoch != epoch`, store `q` as the new baseline, set `key.epoch = epoch`,
   and **emit nothing**. A "mark this frame" flag is not enough: after `ApplyPatchData` a param can
   first register one or more frames later (collapsed nodes register only when driven, and
   mode-hidden params register when their mode is shown), and its restored value would then be
   logged as a hand move.
   Call sites, **before** the data is applied: top of `Undo()` (`src/main.cpp:44190`) and `Redo()`
   (`44221`) on the full-patch path, not the `arrangeOnly` early return (it changes no param), plus
   patch open and `NewPatch()` (`42066`).
5. If `q == last[key]`, skip.
6. `source` = the tag `NoteWriter` left for this key this frame. No tag and an ImGui item active this
   frame or last (`ImGui::IsAnyItemActive()`, cached per frame; the commit frame of a typed value is
   covered by "last frame") → `Hand`. No tag and **nothing active** → `Other`: an in-node button such
   as a preset, randomize or reset that jumps a value without a drag. `Other` is logged but trains at
   weight 0 (README §5).
7. `Modulator` and `Expression` sources are decimated: skip unless ≥ 100 ms since this key's last `VAL`.
8. Push a 16-byte fixed record `{t_ms:u32, id:u32, q:u16, source:u8, flags:u8, pad}` into the SPSC ring.

`BIND` records: emit from `Modulation::Bind`, `Unbind`, `UnbindAllFor`, `SetRange`
(`src/core/Modulation.cpp:9/100/127/41`) through a callback set by `MovementLog::Start`, so
`Modulation` does not include `MovementLog.h`.

`TRANSPORT`: emit when `IsPlaying()` or `Tempo()` changes, and once per bar while playing.

### 1.3 Ring and writer thread

- SPSC ring of 1<<16 fixed records, preallocated. When full, **drop and count** (never block the frame).
- The writer wakes every 2 s (or on Stop). It varint-encodes to `tag · varint id · varint Δt_ms · u16 q`
  (≈ 6 B), and appends to `AppSupportDir()/movement-log/YYYYMMDD-HHMMSS.mlog`.
- File header: magic `IMLOG1`, format version, app version. Records are self-delimiting, so a crash
  truncates only the tail; the reader stops at the first incomplete record.
- `Stop()` drains, writes `MARK SessionEnd` + the dropped count, closes and joins. Reading counts
  before the join raced the worker in `436bd62`; do not repeat that.

### 1.3b Compression and a size cap

- **Compress closed files.** When a session file is closed (on `Stop()`, or on the next launch for a
  file left open by a crash), the writer thread deflates it to `.mlog.z` with **miniz**, which is already
  vendored (`external/miniz.c`). It is in the Windows and Linux source lists in `CMakeLists.txt`
  (~626, ~656) but **not** the macOS list: add it there. The live file stays uncompressed so appends are
  cheap. Expect roughly 2–4× on varint data (an estimate; print the real ratio in the test).
- **Retention cap.** Default 1 GB for the whole `movement-log/` folder. Past that, delete the oldest
  compressed files, but **only after** their contribution is already in `stats.bin` (step 2): the learned
  statistics survive, only the raw history goes. `stats.bin` itself is small (≈ 300 B per key) and is
  never deleted by the cap.
- Settings shows the folder size next to "Open log folder", plus a cap dropdown (256 MB / 1 GB / 4 GB).

### 1.4 Reader + dump tool

`MovementLog::ReadFile(path, callback)` in the same file, plus a `--dump-movement-log <file>` CLI flag
(or `tools/prediction/mlog_dump.py`) that prints records as text. Step 2's evaluator reads through it.

### 1.5 Settings

A "Record movement log" checkbox (default on) and an "Open log folder" button in the Settings window.
These are **owner decisions** (README §12 Q6); build them behind the decision, default on.

## Traps

| Trap | Why it bites here |
|---|---|
| Keying by `nodeIndex` | Undo respawns every node with new indices (`5699b6c`). Use uid. |
| Keying by name | Names repeat within a node (three Wavetable `release`s). Use paramIndex. |
| Logging undo/load jumps as hand moves | Hundreds of fake "moves" on every undo. `NoteMark` + silent re-baseline. |
| Logging during offline render or self-tests | Pollutes the owner's data with fixture motion. Decided once at `Start()`; covers `stats.bin` too. |
| Preset/randomize buttons logged as hand | Big one-frame jumps with no active widget. `Source::Other`, weight 0. |
| Allocating in `Capture` | `std::map` inserts on first sighting are fine; per-frame allocation is not. Reuse buffers. |
| Changing a value | The logger reads only. `invariant-interaction-audit` applies. |
| Linux path | `AppSupportDir()` uses `XDG_CONFIG_HOME` on Linux; logs are data. Note it in the commit; moving to `XDG_DATA_HOME` is an owner call. |

## Test: `INFINITE_MOVELOGTEST`

Add it to `wantsFixture` (`src/main.cpp:62478`) and to `GROUP_MODULATION` in
`.claude/skills/run-infinite-hygiene/driver.sh:205`. Log into a temp dir (env `INFINITE_MOVELOG_DIR`).

1. Spawn an LFO and a node with a continuous param; bind them. Write a different continuous param
   directly (a simulated hand). Run 120 frames.
2. Undo, then redo. Duplicate the node.
3. Stop the log, read it back, and assert:
   - the hand param logs `Hand`, and the LFO-driven one logs `Modulator` at ≤ 10 Hz;
   - no `VAL` burst at the undo or redo frames, and one `MARK` each; a node that is collapsed and
     driven, re-registering two frames after the undo, still logs no `VAL` for its restored value;
   - with an extra unrelated `INFINITE_*` variable set and no `INFINITE_MOVELOG_DIR`, nothing is written anywhere;
   - the key uid is the same before and after undo; the duplicate has a new uid;
   - two same-named params on one node produce two keys.
4. **Perf:** 400 registered params, average `Capture` time < 0.1 ms per frame (print the number).
5. **Size:** after the run, the closed file is compressed; print raw bytes, compressed bytes and the
   ratio. With a cap set to 1 MB and several fake old files, the oldest are deleted, and `stats.bin` is not.

## Exit criterion

`MOVELOGTEST` passes; `run-infinite-hygiene --fast` and the modulation group pass; a 10-minute manual
session produces a readable log < 5 MB raw (compressed after close) with a dropped count of 0.
