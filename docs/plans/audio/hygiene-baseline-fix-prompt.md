# Fix prompt — MEDIADRAGTEST failure + AUDIOPARAMSWEEPTEST false-FAIL blowup

Context for a fresh session with no memory of the investigation that produced
this. Everything below was verified by building the current tree and running
the three tests individually against the compiled binary — the file/line
references are real and current, not recalled.

Reproduce any single check with:

```bash
env INFINITE_<NAME>=1 INFINITE_EXITAFTER=<frames> build/Infinite.app/Contents/MacOS/Infinite
```

frame counts per test are in `.claude/skills/run-infinite-hygiene/driver.sh`'s
`TESTS` array.

---

## 1. `SettingsDir()` doesn't redirect for MEDIADRAGTEST — data loss, and it's
##    why MEDIADRAGTEST fails

**File:** `src/audio/SampleScanner.cpp:22-38` (`SettingsDir()`)

The throwaway-settings redirect there is gated on `INFINITE_SAMPLERDRAGTEST`
only:

```cpp
if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
   dir += "/sampler_drag_test";
```

`INFINITE_MEDIADRAGTEST` has no equivalent, so the Media fixture at
`src/main.cpp:17192-17217` runs its real `RemoveFolder`/`AddFolder`/`StartScan`
calls against the user's actual `~/Library/Application Support/Infinite/
MediaFolders.json` and `MediaIndex.json`. Two consequences, both confirmed:

**a) It destroys the user's media folder list.** `RemoveFolder`
(`src/audio/SampleScanner.cpp:116`) calls `SaveFoldersToDisk()`, so the
fixture's "drop stale folders" loop rewrites the real file. On the machine
this was investigated on, `MediaFolders.json` was left containing only
`/tmp/infinite_mediadrag` — the real entry (`/Users/namansoni/Downloads`) was
gone. It has been manually restored, but every hygiene run wipes it again
until this is fixed. This is exactly the failure the existing comment at
`SampleScanner.cpp:28-33` says the Sampler redirect was added to prevent; the
Media case was simply never covered.

**b) It's the actual cause of `MEDIADRAGTEST FAIL`.** `RemoveFolder`
deliberately leaves index entries in place (see its comment,
`SampleScanner.cpp:119-122`), so removing the folders does *not* empty
`mIndex` — the persisted real index survives. Instrumenting
`DrawLibrarySearchPanel`'s row loop (`src/main.cpp:5880-5917`) showed
`scanner.Index().size() == 467` and the last row's `ImGui::GetItemRectMin/Max`
coming back as `(1176,885)-(1430,885)` — a **zero-height, clipped rect** for a
row scrolled far below the child's visible area. `gMediaDragTestRowRect` latches
that degenerate rect (it's assigned unconditionally per row, so the last row
wins), the driver's phase 0 accepts it because it only checks `.x >= 0.0f`
(`src/main.cpp:18779`), and the synthetic press then lands on nothing:
`ImGui::IsItemActive()` never becomes true, `gSampleDragActive` is never set,
the release handler at `src/main.cpp:25676` never runs, and
`ImageSourceNode::LoadedPath()` is still empty at the verdict.

SAMPLERDRAGTEST passes only because its redirect works — its throwaway dir
starts empty, so the panel shows exactly one row: the fixture's own file.

**Fix:**

1. Extend `SettingsDir()` to redirect for `INFINITE_MEDIADRAGTEST` too, into
   its own subdirectory (`/media_drag_test`, parallel to `/sampler_drag_test`
   — a separate dir rather than a shared one, so the two tests can't see each
   other's index). Update the comment there to name both env vars.
2. Tighten the phase-0 gate at `src/main.cpp:18779` to reject a degenerate row
   rect — require a real height (e.g. `gMediaDragTestRowRect.w -
   gMediaDragTestRowRect.y > 1.0f`) before latching the gesture. Do the same
   for `gSamplerDragTestRowRect` at `src/main.cpp:18684`-ish for symmetry. This
   is the belt-and-braces half: without it, any future condition that makes
   rows clip again produces the same silent "drove a gesture at nothing"
   failure instead of a legible one.
3. Consider having the fixtures print a diagnostic when they give up
   (`EXITAFTER` elapsed with `sPhase == 0`) rather than exiting with no verdict
   line at all — today a fixture that never finds a usable row prints nothing,
   which the driver reads as a pass.

This item is confirmed root cause, not a hypothesis. Fix (1) alone should turn
MEDIADRAGTEST green; verify by running it after the change.

---

## 2. AUDIOPARAMSWEEPTEST — 23 actionable `[FAIL]`s (of 237 reported)

The baseline text in `.claude/skills/run-infinite-hygiene/driver.sh:176-201`
documents 15 known false-FAILs (Dynamics `sidechainExternal` ×1, Delay `sync`/
`rateDiv` ×2, Reverb `decay`/`damping`/`predelay` ×3, Sampler ×9). The current
tree reports **237** total. Of those:

- **199 are Drum Sequencer — out of scope, see below.**
- 15 are the documented baseline above, unchanged.
- **23 are new and actionable** — the subject of this section.

All are check-B (`reaches audio thread within one block`); check A (save/load
round trip) passes for every node. So nothing here is a param-plumbing
regression — they are blind spots in the sweep rig itself.

### 2a. Drum Sequencer's 199 — DO NOT TOUCH

Drum Sequencer is under active development in a **separate concurrent
session**. Its 199 `[FAIL]`s are expected while that work is in flight, and
they are not this prompt's problem.

Do not edit `src/nodes/DrumSequencerNode.cpp` or
`src/nodes/DrumSequencerNode.h` under any circumstances — the other session
owns those files and any change here becomes a merge conflict.

Two constraints that follow from that, and that you do need to honor:

1. When you rewrite the driver's baseline text (item 2e), record Drum
   Sequencer's FAILs as **temporary, pending the in-flight node work** — not
   as a permanent documented baseline. Someone has to come back and re-run the
   sweep once that node lands.
2. Any change you make to the shared sweep rig (`src/main.cpp`'s
   `AudioParamSweep` namespace) must not special-case Drum Sequencer. If a
   generic improvement you make for item 2b — a longer measurement window, a
   chord instead of a single note — happens to recover some Drum Sequencer
   params, that's fine and welcome. Deliberately engineering the rig around
   that node right now is not: its shape is still changing underneath you.

For the record, so the eventual follow-up session doesn't have to re-derive
it: Drum Sequencer is silent in the rig for three independent reasons — an
all-zero default `stepVel` grid, null voice buffers, and a `Transport` clock
that never advances because the rig calls `AudioNode::ProcessBlock` directly
rather than going through `AudioEngine::Process`. Relevant detail: the rig
pushes note 69 while `baseNote` defaults to 36, so the note-inbox trigger path
computes an out-of-range `lane = 33` and drops it. That's a note for later,
not work for now.

### 2b. Note-processor nodes — 18 params

Arpeggiator ×6 (`mode`, `octaves`, `rateMode`, `rateBeats`, `rateSeconds`,
`gatePercent`), Note Echo ×3 (`repeats`, `decay`, `transposePerRepeat`), Note
Modify ×3 (`velocityCurve`, `humanizeTimingMs`, `glideMs`), Note Capturer ×2
(`loop`, `quantizeDiv`), Note Router ×1 (`probability`), Velocity Curve ×1
(`curve`), Humanizer ×1 (`timingMs`), Glide ×1 (`glideMs`).

All are limits of the `kNoteOutbox` read mode at `src/main.cpp:16527-16545`,
which builds its signature from **one block, one note-on, three scalars**:
event count, first event's `note`, first event's `velocity`. That signature is
structurally blind to:

- **`frameOffset`** — so every timing/humanize param (`humanizeTimingMs`,
  `timingMs`) is invisible by construction.
- **Anything after the first block** — so echo repeats (fired `delayMs`
  later), arpeggiator steps (fired at rate intervals), and glide ramps all land
  outside the measurement window. Note Echo's `delayMs` passes only because it
  happens to shift the *first* event.
- **Velocity at a curve's fixed point** — the rig drives velocity `1.0`
  (`src/main.cpp:16480`), and velocity curves map 1.0 → 1.0 under any shape, so
  `velocityCurve` and `curve` can never move the signature.
- **A single held note** — arpeggiator `mode` and `octaves` need a chord and
  several steps to differentiate.
- **Which outbox port an event went to** — `Note Router`'s `probability`
  redistributes across ports, but only `NoteOutbox()` (port 0) is read.

**Recommended fix**, in the note-outbox read mode:

1. Widen `Signature` for note mode: hash every popped event's `note`,
   `velocity`, `isNoteOn`, **and `frameOffset`**, not just the first event's
   three fields. The `values[4]` array is the constraint — either widen it or
   fold the hash into one float channel.
2. Accumulate the signature across **N blocks** (8–16) after the alteration,
   not one, so delayed/arpeggiated events land inside the window. The
   `warmUpAndAlter` lambda at `src/main.cpp:16612-16625` already runs 12 warmup
   blocks; mirror that count on the measurement side.
3. Drive velocity at `0.5` rather than `1.0`, so velocity-shaping params have
   somewhere to move.
4. Hold a 3-note chord (e.g. 60/64/67 relative to whatever the node expects)
   rather than a single note 69, so mode/octaves/order params differentiate.

Each of these four is a separate observable improvement — apply them
incrementally and re-run the sweep after each so it's clear which params each
one recovers. Any that remain `[FAIL]` after all four are genuine rig blind
spots and belong in the documented baseline with a stated reason.

### 2c. `Stereo` → `width` — 1 param

`FillDriveTone` (`src/main.cpp:16454-16462`) writes the **identical** sample to
both `l[i]` and `r[i]`. A perfectly correlated mono signal has no stereo width
to widen, so a width control cannot change the output — this is a rig defect,
not a node defect. Fix: decorrelate the drive tone (different frequency or a
phase offset on the right channel). Check the sweep still passes for nodes that
currently rely on L==R before committing this; it's a global change to the
excitation signal.

### 2d. `rateDiv` on Chorus / Flanger / Phaser / Stutter — 4 params

Same shape as the already-documented Delay `sync`/`rateDiv` baseline: a
tempo-sync division whose effect either sits behind a `sync` prerequisite or
plays out over a modulation period longer than the sweep's measurement window.
Check whether `EffectDefs.cpp` declares a `prerequisites` entry pointing
`rateDiv` at its node's `sync` bool (the mechanism at
`src/main.cpp:16561-16588` already exists for exactly this) — if not, adding
one may fix these outright. If it doesn't, they belong in the documented
baseline alongside Delay's.

### 2e. Update the baseline text

`.claude/skills/run-infinite-hygiene/driver.sh:176-201` must be rewritten to
match whatever survives after the above. Also update
`.claude/skills/audio-node-sweep/SKILL.md`'s blind-spots section — the
"signature is blind to frameOffset and to events beyond block 1" limitation is
the most reusable finding here and isn't documented anywhere today.

---

## 3. DRAGTEST — passes; the baseline note about it is stale

`.claude/skills/run-infinite-hygiene/driver.sh:171-172` says DRAGTEST's
canvas-pan sub-check prints `"... : BUG"` on a clean tree. On the current tree
it prints:

```
after canvas drag: node pos=(60,60) viewAnchor=(168,149) -> node still, view panned : PAN OK
after node drag:   node pos = (200, 160)  MOVED OK
```

Both sub-checks pass. Delete that stale sentence from the driver's summary
text so a future run doesn't excuse a real DRAGTEST regression as "known
baseline." Do not change the test itself.

---

## Out of scope

- **Drum Sequencer, in every form** — see item 2a. Another session is building
  that node right now and owns both of its files.
- Do not touch the documented Sampler (9), Dynamics `sidechainExternal`,
  Reverb (3), or Delay (2) baseline FAILs unless item 2d's prerequisite check
  incidentally fixes Delay's. They were hand-confirmed correct; see
  `src/audio/EffectDefs.cpp`'s per-param comments.
- Do not restructure `DrawLibrarySearchPanel` or the drag/drop release handler
  at `src/main.cpp:25676` — the drop path itself is correct and symmetric
  between the Sampler and Media branches. The bug is entirely upstream, in
  which settings directory the scanner reads.
- The `PHASEATEST` "Smooth" node-name collision mentioned in the driver's
  baseline text is a separate pre-existing issue and not part of this work.

---

## Done criterion

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

must compile clean, then:

```bash
.claude/skills/run-infinite-hygiene/driver.sh --skip-build
```

MEDIADRAGTEST must report `[pass]`.

AUDIOPARAMSWEEPTEST will still report `[FAIL]` — Drum Sequencer's 199 are
expected until the concurrent node work lands. What must be true is that
**every non-Drum-Sequencer FAIL is explained param by param** in the driver's
rewritten baseline text, with a one-line stated reason, or converted to a
`[SKIP]`. Count them explicitly before and after so the delta is legible:
the starting point is 38 non-Drum-Sequencer FAILs (15 documented baseline +
23 actionable).

Confirm `~/Library/Application Support/Infinite/MediaFolders.json` is
untouched after the run — that's the data-loss regression test for item 1.

`git status` must show no changes to `src/nodes/DrumSequencerNode.{h,cpp}`.
